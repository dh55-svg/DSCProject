#include "ModbusComm.h"
#include "logger.h"

#include <modbus.h>
#include <QDateTime>

ModbusComm::ModbusComm(QObject* parent) : QObject(parent)
{
    m_connected.storeRelaxed(0);
    m_polling.storeRelaxed(0);
    m_reconnectiong.storeRelaxed(0);

    m_pollTimer = new QTimer(this);
    m_pollTimer->setSingleShot(false);
    connect(m_pollTimer, &QTimer::timeout, this, &ModbusComm::onPollTimeout);

    m_heartbeatTimer = new QTimer(this);
    m_heartbeatTimer->setSingleShot(false);
    connect(m_heartbeatTimer, &QTimer::timeout, this, &ModbusComm::onHeartbeatTimeout);
    LOG_INFO("ModbusComm", "Modbus通讯模块初始化完成，libmodbus版");
}

ModbusComm::~ModbusComm()
{
    stopPoll();
    disconnect();
}

bool ModbusComm::connectToHost(const ModbusConfig& config)
{
    m_config = config;
    if (!createContext(config))
    {
        emit connectionError(QString("创建Modbus上下文失败"));
        return false;
    }
    if (modbus_connect(m_ctx) == -1)
    {
        QString error = QString::fromUtf8(modbus_strerror(errno));
        LOG_ERROR("ModbusComm", QString("连接失败: %1").arg(error));
        emit connectionError(error);
        destoryContext();
        return false;
    }
    m_connected.storeRelaxed(1);
    m_heartbeatFailCount = 0;
    m_heartbeatTimer->start(config.heartbeatInterval);
    emit connectionEstablished();
    LOG_INFO("ModbusComm", "Modbus连接已建立，libmodbus版");
    return true;
}

void ModbusComm::disconnect()
{
    stopPoll();
    m_heartbeatTimer->stop();
    m_connected.storeRelaxed(0);
    m_reconnectiong.storeRelaxed(0);
    destoryContext();
    LOG_INFO("ModbusComm", "Modbus连接已断开");
}

bool ModbusComm::isConnected() const
{
    return m_connected.loadRelaxed() != 0;
}

bool ModbusComm::readHoldingRegisters(int serverAddress, int startAddress, int count, QVector<quint16>& values)
{
    if (!isConnected() || !m_ctx) return false;
    if (modbus_set_slave(m_ctx, serverAddress) == -1)
    {
        LOG_WARN("ModbusComm", QString("设置从站地址失败: %1").arg(serverAddress));
        return false;
    }
    values.resize(count);
    int rc = modbus_read_registers(m_ctx, startAddress, count, values.data());
    if (rc == -1)
    {
        LOG_WARN("ModbusComm", QString("读取保持寄存器失败: 从站=%1, 起始=%2, 数量=%3, 错误=%4")
            .arg(serverAddress).arg(startAddress).arg(count)
            .arg(modbus_strerror(errno)));
        return false;
    }
    return true;
}

bool ModbusComm::readInputRegisters(int serverAddress, int startAddress, int count, QVector<quint16>& values)
{
    if (!isConnected() || !m_ctx) {
        return false;
    }

    if (modbus_set_slave(m_ctx, serverAddress) == -1) {
        return false;
    }

    values.resize(count);
    int rc = modbus_read_input_registers(m_ctx, startAddress, count, values.data());

    if (rc == -1) {
        LOG_WARN("ModbusComm", QString("读取输入寄存器失败: 从站=%1, 起始=%2, 数量=%3")
            .arg(serverAddress).arg(startAddress).arg(count));
        return false;
    }

    return true;
}

bool ModbusComm::writeHoldingRegister(int serverAddress, int address, quint16 value)
{
    return writeHoldingRegisters(serverAddress, address, { value });
}

bool ModbusComm::writeHoldingRegisters(int serverAddress, int address, const QVector<quint16>& values)
{
    QMutexLocker lock(&m_writeMutex);
    WriteTask task;
    task.serverAddress = serverAddress;
    task.address = address;
    task.values = values;

    m_writeQueue.enqueue(task);
    if (!m_writeInProgress)
    {
        m_writeInProgress = true;
        lock.unlock();
        processWriteQueue();
    }
    return true;
}

void ModbusComm::setPollConfig(int serverAddress, int startAddress, int count)
{
    m_pollServerAddress = serverAddress;
    m_poolStartAddress = startAddress;
    m_poolCount = count;
}

void ModbusComm::startPoll()
{
    if (m_polling.loadRelaxed())
    {
        return;
    }
    m_polling.storeRelaxed(1);
    m_pollTimer->start(m_config.poolInterval);
    LOG_INFO("ModbusComm", QString("开始轮询: 从站=%1, 起始=%2, 数量=%3, 间隔=%4ms")
        .arg(m_pollServerAddress).arg(m_poolStartAddress)
        .arg(m_poolCount).arg(m_config.poolInterval));
}

void ModbusComm::stopPoll()
{
    m_polling.storeRelaxed(0);
    m_pollTimer->stop();
    LOG_INFO("ModbusComm", "停止轮询");
}

bool ModbusComm::isPolling() const
{
    return m_polling.loadRelaxed() != 0;
}

void ModbusComm::onPollTimeout()
{
    if (!isConnected() || !m_ctx) return;
    QVector<quint16> values;
    if (readHoldingRegisters(m_pollServerAddress, m_poolStartAddress, m_poolCount, values))
    {
        emit dataReceived(m_pollServerAddress, m_poolStartAddress, values);
    }
}

void ModbusComm::onHeartbeatTimeout()
{
    if (!m_ctx) return;
    QVector<quint16> values;
    bool ok = readHoldingRegisters(m_pollServerAddress, m_poolStartAddress, m_poolCount, values);
    if (!ok)
    {
        m_heartbeatFailCount++;
        LOG_WARN("ModbusComm", QString("心跳检测失败 (%1/%2)")
            .arg(m_heartbeatFailCount).arg(HEARTBEAT_MAX_FAIL));
        if (m_heartbeatFailCount >= HEARTBEAT_MAX_FAIL)
        {
            m_connected.storeRelaxed(0);
            stopPoll();
            emit connectionLost();
            LOG_ERROR("ModbusComm", "通信中断，准备自动重连...");

            attemptReconnect();
        }
    }
    else {
        m_heartbeatFailCount = 0;
    }
}

bool ModbusComm::createContext(const ModbusConfig& config)
{
    destoryContext();
    if (config.type == ModbusConfig::Tcp)
    {
        m_ctx = modbus_new_tcp(config.host.toUtf8().constData(), config.port);
        if (!m_ctx)
        {
            LOG_ERROR("ModbusComm", QString("创建TCP上下文失败: %1").arg(modbus_strerror(errno)));
            return false;
        }
    }
    else {
        m_ctx = modbus_new_rtu(config.portName.toUtf8().constData(), config.baudRate, config.parity, config.dataBit, config.stopBit);
        if (!m_ctx)
        {
            LOG_ERROR("ModbusComm", QString("创建RTU上下文失败: %1").arg(modbus_strerror(errno)));
            return false;
        }
        LOG_INFO("ModbusComm", QString("创建Modbus RTU通讯模块: %1, 波特率: %2")
            .arg(config.portName).arg(config.baudRate));
    }
    uint32_t toSec = config.timeout / 1000;
    uint32_t toUSec = (config.timeout % 1000) * 1000;  // 将毫秒余数转为微秒
    modbus_set_response_timeout(m_ctx, toSec, toUSec);

    modbus_set_byte_timeout(m_ctx, toSec / 2, toUSec / 2);

    // 启用错误恢复：连接断开时自动尝试恢复
    modbus_set_error_recovery(m_ctx,
        static_cast<modbus_error_recovery_mode>(
            MODBUS_ERROR_RECOVERY_LINK | MODBUS_ERROR_RECOVERY_PROTOCOL));

    return true;
}

void ModbusComm::destoryContext()
{
    if (m_ctx)
    {
        modbus_close(m_ctx);
        modbus_free(m_ctx);
        m_ctx = nullptr;
    }
}

void ModbusComm::processWriteQueue()
{
    if (!isConnected() || !m_ctx) {
        QMutexLocker lock(&m_writeMutex);
        m_writeInProgress = false;
        return;
    }

    while (true) {
        QMutexLocker lock(&m_writeMutex);
        if (m_writeQueue.isEmpty()) {
            m_writeInProgress = false;
            return;
        }
        WriteTask task = m_writeQueue.dequeue();
        lock.unlock();

        if (modbus_set_slave(m_ctx, task.serverAddress) == -1) {
            LOG_WARN("ModbusComm", QString("写队列设从站地址失败: 从站=%1").arg(task.serverAddress));
            emit writeCompleted(task.serverAddress, task.address, false);
            continue;
        }

        int rc = modbus_write_registers(m_ctx, task.address,
            task.values.size(),
            reinterpret_cast<const uint16_t*>(task.values.constData()));
        if (rc == -1) {
            LOG_ERROR("ModbusComm", QString("写队列写入失败: 从站=%1, 地址=%2, 错误=%3")
                .arg(task.serverAddress).arg(task.address)
                .arg(QString::fromUtf8(modbus_strerror(errno))));
            emit writeCompleted(task.serverAddress, task.address, false);
            // 写入失败立即停止，避免连续发送错误命令
            QMutexLocker failLock(&m_writeMutex);
            m_writeInProgress = false;
            return;
        }

        emit writeCompleted(task.serverAddress, task.address, true);
    }
}

void ModbusComm::attemptReconnect()
{
    if (m_reconnectiong.loadRelaxed()) return;
    m_reconnectiong.storeRelaxed(1);
    QTimer::singleShot(2000, this, [this]() {
        LOG_INFO("ModbusComm", "正在重连...");
        destoryContext();
        if (connectToHost(m_config)) {
            m_reconnectiong.storeRelaxed(0);
            LOG_INFO("ModbusComm", "重连成功，自动开始轮询");
            startPoll();
        }
        else {
            m_reconnectiong.storeRelaxed(0);
            LOG_ERROR("ModbusComm", "重连失败，5秒后再次尝试");
            QTimer::singleShot(5000, this, &ModbusComm::attemptReconnect);
        }
        });
}
