#include "ModbusImpl.h"
#include <QThread>
#include <qdebug.h>

ModbusImpl::ModbusImpl(QObject* parent) : QObject(parent) {}

ModbusImpl::~ModbusImpl() {
    stopAll();
}

void ModbusImpl::addDevice(const DeviceConfig& cfg) {
    DeviceEntry entry;
    entry.config = cfg;
    entry.comm = new ModbusComm();
    entry.thread = new QThread(this);

    ModbusConfig mcfg;
    mcfg.type = ModbusConfig::Tcp;
    mcfg.host = cfg.ip;
    mcfg.port = cfg.port;
    mcfg.poolInterval = cfg.pollIntervalMs;

    entry.comm->setPollConfig(cfg.serverAddr, cfg.regStart, cfg.regCount);
    entry.comm->moveToThread(entry.thread);

    connect(entry.comm, &ModbusComm::dataReceived, this, &ModbusImpl::onDataReceived, Qt::DirectConnection);
    connect(entry.comm, &ModbusComm::connectionEstablished, this, [this, id = cfg.deviceId]() {
        emit deviceOnline(id);
    });
    connect(entry.comm, &ModbusComm::connectionLost, this, [this, id = cfg.deviceId]() {
        emit deviceOffline(id);
        checkAllOffline();
    });
    connect(entry.thread, &QThread::finished, entry.comm, &QObject::deleteLater);
    connect(entry.thread, &QThread::finished, entry.thread, &QObject::deleteLater);

    entry.thread->start();
    QMetaObject::invokeMethod(entry.comm, [entry, mcfg]() {
        entry.comm->connectToHost(mcfg);
        entry.comm->startPoll();
    }, Qt::QueuedConnection);

    m_devices.append(entry);
}

void ModbusImpl::startAll() {
    for (auto& dev : m_devices) {
        if (dev.comm && dev.comm->isConnected()) {
            QMetaObject::invokeMethod(dev.comm, "startPoll", Qt::QueuedConnection);
        }
    }
}

void ModbusImpl::stopAll() {
    for (auto& dev : m_devices) {
        if (dev.comm) {
            QMetaObject::invokeMethod(dev.comm, "stopPoll", Qt::QueuedConnection);
        }
    }
}

void ModbusImpl::writeRegister(int devId, int addr, quint16 val) {
    for (auto& dev : m_devices) {
        if (dev.config.deviceId == devId && dev.comm) {
            QMetaObject::invokeMethod(dev.comm, [dev, addr, val]() {
                dev.comm->writeHoldingRegister(dev.config.serverAddr, addr, val);
            }, Qt::QueuedConnection);
            return;
        }
    }
}

void ModbusImpl::setDataSink(IMessageBus* sink) {
    m_sink = sink;
}

bool ModbusImpl::isDeviceConnected(int devId) const {
    for (auto& dev : m_devices) {
        if (dev.config.deviceId == devId)
            return dev.comm && dev.comm->isConnected();
    }
    return false;
}

int ModbusImpl::onlineDeviceCount() const {
    int count = 0;
    for (auto& dev : m_devices) {
        if (dev.comm && dev.comm->isConnected()) count++;
    }
    return count;
}

int ModbusImpl::totalDeviceCount() const {
    return m_devices.size();
}

void ModbusImpl::onDataReceived(int serverAddr, int startAddr, const QVector<quint16>& values) {
    if (!m_sink) return;
    RawModbusData raw;
    raw.serverAddr = serverAddr;
    raw.startAddr = startAddr;
    raw.count = values.size();
    for (int i = 0; i < values.size() && i < 128; ++i) {
        raw.values[i] = values[i];
    }
    m_sink->enqueue(raw);
}

void ModbusImpl::checkAllOffline() {
    if (onlineDeviceCount() == 0) {
        emit allDevicesOffline();
    }
}
