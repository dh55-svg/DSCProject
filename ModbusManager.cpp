#include "ModbusManager.h"
#include "logger.h"

ModbusManager::ModbusManager(QObject* parent) : QObject(parent)
{
    m_running.storeRelaxed(0);
    m_totalEnqueued.storeRelaxed(0);
    m_totalDropped.storeRelaxed(0);
}

ModbusManager::~ModbusManager()
{
    stopAll();
    for (auto it = m_device.begin(); it != m_device.end(); ++it)
    {
        DeviceConfig& ctx = it.value();
        if (ctx.thread)
        {
            ctx.thread->quit();
            ctx.thread->wait(3000);
            if (ctx.thread->isRunning())
            {
                ctx.thread->terminate();
                ctx.thread->wait();
            }
        }
        if (ctx.comm)
        {
            ctx.comm->disconnect();
            delete ctx.comm;
            ctx.comm = nullptr;
        }
        delete ctx.thread;
        ctx.thread = nullptr;
    }
    LOG_INFO("ModbusManager", QString("Modbus管理器已释放，总入队=%1，总丢失=%2")
        .arg(m_totalEnqueued.loadRelaxed())
        .arg(m_totalDropped.loadRelaxed()));
}

void ModbusManager::setRingBuffer(LockFreeRingBuffer<RawModbusData, 8192>* queue)
{
    m_ringBuffer = queue;
    LOG_INFO("ModbusManager", "已设置环形缓冲区");
}

bool ModbusManager::addDevice(const ModbusDeviceConfig& config)
{
    QMutexLocker lock(&m_devicemutex);
    if (m_device.contains(config.deviceId))
    {
        LOG_ERROR("ModbusManager", "Device已经存在");
        return false;
    }
    if (m_device.size() >= 12)
    {
        LOG_ERROR("ModbusManager", "设备数量已达上限(12)");
        return false;
    }
    DeviceConfig ctx;
    ctx.deviceId = config.deviceId;
    ctx.config = config;
    ctx.connected = false;
    ctx.thread = new QThread();
    ctx.thread->setObjectName(QString("ModbusThread-%1").arg(ctx.deviceId));

    ctx.comm = new ModbusComm();
    ctx.comm->setPollConfig(config.pollServerAddress, config.pollStartAddress, config.pollCount);

    ctx.comm->moveToThread(ctx.thread);
    connect(ctx.comm, &ModbusComm::dataReceived, this, &ModbusManager::onDataReceived, Qt::DirectConnection);
    // 设备状态变化用QueuedConnection保证线程安全
    connect(ctx.comm, &ModbusComm::connectionLost,
        this, &ModbusManager::onDeviceConnectionLost,
        Qt::QueuedConnection);

    connect(ctx.comm, &ModbusComm::connectionEstablished,
        this, &ModbusManager::onDeviceConnectionEstablished,
        Qt::QueuedConnection);

    ctx.thread->start();

    // 在目标线程中执行连接操作
    int captureId = config.deviceId;
    QMetaObject::invokeMethod(ctx.comm, [this, captureId]() {
        QMutexLocker deviceLock(&m_devicemutex);
        auto it = m_device.find(captureId);
        if (it != m_device.end() && it.value().comm) {
            it.value().comm->connectToHost(it.value().config.modbusConfig);
        }
        });

    // 存入map
    m_device.insert(config.deviceId, std::move(ctx));

    LOG_INFO("ModbusManager", QString("添加设备: ID=%1, 名称=%2, 地址=%3:%4, 从站=%5, 寄存器=%6-%7")
        .arg(config.deviceId)
        .arg(config.deviceName)
        .arg(config.modbusConfig.host)
        .arg(config.modbusConfig.port)
        .arg(config.pollServerAddress)
        .arg(config.pollStartAddress)
        .arg(config.pollStartAddress + config.pollCount - 1));
    return true;
}

bool ModbusManager::removeDevice(int deviceId)
{
    QMutexLocker lock(&m_devicemutex);
    auto it = m_device.find(deviceId);
    if (it == m_device.end()) return false;
    DeviceConfig& ctx = it.value();

    // 在目标线程中停止轮询和断开连接
    ModbusComm* commPtr = ctx.comm;
    QThread* threadPtr = ctx.thread;
    if (commPtr) {
        QMetaObject::invokeMethod(commPtr, [commPtr]() {
            commPtr->stopPoll();
            commPtr->disconnect();
            });
    }
    if (threadPtr)
    {
        threadPtr->quit();
        threadPtr->wait(3000);
    }
    delete commPtr;
    delete threadPtr;

    m_device.erase(it);
    LOG_INFO("ModbusManager", QString("移除设备: ID=%1").arg(deviceId));
    return true;
}

void ModbusManager::startAll()
{
    m_running.storeRelaxed(1);
    for (auto it = m_device.begin(); it != m_device.end(); it++)
    {
        auto& ctx = it.value();
        QMetaObject::invokeMethod(ctx.comm, [comm = ctx.comm]() {
            comm->startPoll();
            }, Qt::QueuedConnection);
    }
    LOG_INFO("ModbusManager", QString("启动全部设备轮询，设备数=%1").arg(m_device.size()));
}

void ModbusManager::stopAll()
{
    m_running.storeRelaxed(0);
    for (auto it = m_device.begin(); it != m_device.end(); it++)
    {
        auto& ctx = it.value();
        QMetaObject::invokeMethod(ctx.comm, [comm = ctx.comm]() {
            comm->stopPoll();
            }, Qt::QueuedConnection);
    }
    LOG_INFO("ModbusManager", "停止全部设备轮询");
}

bool ModbusManager::startDevice(int deviceId)
{
    QMutexLocker lock(&m_devicemutex);
    auto it = m_device.find(deviceId);
    if (it == m_device.end())
        return false;

    DeviceConfig& ctx = it.value();
    QMetaObject::invokeMethod(ctx.comm, [comm = ctx.comm]() {
        comm->startPoll();
        }, Qt::QueuedConnection);
    LOG_INFO("ModbusManager", QString("启动设备轮询: ID=%1").arg(deviceId));
    return true;
}

bool ModbusManager::stopDevice(int deviceId)
{
    QMutexLocker lock(&m_devicemutex);
    auto it = m_device.find(deviceId);
    if (it == m_device.end())
        return false;

    DeviceConfig& ctx = it.value();
    QMetaObject::invokeMethod(ctx.comm, [comm = ctx.comm]() {
        comm->stopPoll();
        }, Qt::QueuedConnection);
    LOG_INFO("ModbusManager", QString("停止设备轮询: ID=%1").arg(deviceId));
    return true;
}

bool ModbusManager::isDeviceConnected(int deviceId) const
{
    QMutexLocker lock(&m_devicemutex);
    auto it = m_device.find(deviceId);
    if (it == m_device.end())
        return false;
    return it.value().connected;
}

QVector<int> ModbusManager::deviceIds() const
{
    QMutexLocker lock(&m_devicemutex);
    return m_device.keys().toVector();
}

int ModbusManager::onlineDeviceCount() const
{
    QMutexLocker lock(&m_devicemutex);
    int count = 0;
    for (auto it = m_device.begin(); it != m_device.end(); ++it)
    {
        const DeviceConfig& ctx = it.value();
        if (ctx.connected)
        {
            count++;
        }
    }
    return count;
}

int ModbusManager::totalDeviceCount() const
{
    QMutexLocker locker(&m_devicemutex);
    return m_device.size();
}

bool ModbusManager::writeRegister(int deviceId, int serverAddress, int address, quint16 value)
{
    QMutexLocker lock(&m_devicemutex);
    auto it = m_device.find(deviceId);
    if (it == m_device.end())
    {
        LOG_WARN("ModbusManager", QString("写入失败，设备不存在 ID=%1").arg(deviceId));
        return false;
    }
    if (!it.value().connected)
    {
        LOG_WARN("ModbusManager", QString("写入失败，设备离线 ID=%1").arg(deviceId));
        return false;
    }
    DeviceConfig& ctx = it.value();
    QMetaObject::invokeMethod(ctx.comm, [comm = ctx.comm, serverAddress, address, value]() {
        comm->writeHoldingRegister(serverAddress, address, value);
        }, Qt::QueuedConnection);
    return true;
}

void ModbusManager::onDataReceived(int serverAddress, int startAddress, const QVector<quint16>& values)
{
    if (!m_ringBuffer) return;
    RawModbusData raw;
    raw.serverAddress = serverAddress;
    raw.startAddress = startAddress;
    raw.timestamp = QDateTime::currentMSecsSinceEpoch();
    raw.valueCount = qMin(values.size(), 128);

    for (int i = 0; i < raw.valueCount; i++)
    {
        raw.values[i] = values[i];
    }
    ModbusComm* sendercomm = qobject_cast<ModbusComm*>(sender());
    for (auto it = m_device.constBegin(); it != m_device.constEnd(); ++it)
    {
        if (it.value().comm == sendercomm)
        {
            raw.deviceId = it.key();
            break;
        }
    }
    if (!m_ringBuffer->enqueue(raw))
    {
        m_totalDropped.fetchAndAddRelaxed(1);
        int drop = m_totalDropped.loadRelaxed();
        if (drop % 100 == 0)
        {
            LOG_WARN("ModbusManager",
                QString("环形缓冲区已满，数据丢失! 总丢失=%1，请检查DataParseThread消费速度")
                .arg(drop));
        }
    }
    else {
        m_totalEnqueued.fetchAndAddRelease(1);
    }
}

void ModbusManager::onDeviceConnectionLost()
{
    ModbusComm* comm = qobject_cast<ModbusComm*>(sender());
    if (!comm) return;
    for (auto it = m_device.begin(); it != m_device.end(); ++it)
    {
        if (it.value().comm == comm)
        {
            it.value().connected = false;
            emit deviceStatusChanged(it.key(), false);
            LOG_ERROR("ModbusManager", QString("设备离线: ID=%1, 名称=%2")
                .arg(it.key()).arg(it.value().config.deviceName));
            if (onlineDeviceCount() == 0) emit allDevicesOffline();
            break;
        }
    }
}

void ModbusManager::onDeviceConnectionEstablished()
{
    ModbusComm* comm = qobject_cast<ModbusComm*>(sender());
    if (!comm) return;
    for (auto it = m_device.begin(); it != m_device.end(); ++it)
    {
        if (it.value().comm == comm)
        {
            it.value().connected = true;
            emit deviceStatusChanged(it.key(), true);
            QMetaObject::invokeMethod(comm, [comm]()
                {
                    comm->startPoll();
                }, Qt::QueuedConnection);
            LOG_INFO("ModbusManager", QString("设备上线: ID=%1, 名称=%2, 自动开始轮询")
                .arg(it.key()).arg(it.value().config.deviceName));
            break;
        }
    }
}
