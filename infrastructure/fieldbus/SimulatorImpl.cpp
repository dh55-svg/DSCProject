#include "SimulatorImpl.h"
#include "infrastructure/messaging/IMessageBus.h"

SimulatorImpl::SimulatorImpl(QObject* parent) : QObject(parent) {
    connect(&m_timer, &QTimer::timeout, this, &SimulatorImpl::onTick);
}

SimulatorImpl::~SimulatorImpl() {
    stopAll();
}

void SimulatorImpl::addDevice(const DeviceConfig& cfg) {
    SimDevice dev;
    dev.config = cfg;
    for (int i = 0; i < cfg.regCount; ++i) {
        float phase = (float)i * 0.3f + (float)cfg.deviceId * 1.2f;
        dev.tagPhases.append({cfg.regStart + i, phase});
    }
    m_devices.append(dev);
}

void SimulatorImpl::startAll() {
    m_running = true;
    m_timer.start(500);
}

void SimulatorImpl::stopAll() {
    m_running = false;
    m_timer.stop();
}

void SimulatorImpl::writeRegister(int devId, int addr, quint16 val) {
    // Demo mode: writes are no-ops
    Q_UNUSED(devId);
    Q_UNUSED(addr);
    Q_UNUSED(val);
}

void SimulatorImpl::setDataSink(IMessageBus* sink) {
    m_sink = sink;
}

void SimulatorImpl::onTick() {
    if (!m_sink) return;
    m_tick++;

    for (auto& dev : m_devices) {
        RawModbusData raw;
        raw.serverAddr = dev.config.serverAddr;
        raw.startAddr = dev.config.regStart;
        raw.count = dev.config.regCount;

        for (int i = 0; i < dev.tagPhases.size(); ++i) {
            float phase = dev.tagPhases[i].second;
            float val = 50.0f + 30.0f * qSin((m_tick * 0.1f) + phase);
            val += 5.0f * qSin((m_tick * 0.02f) + phase * 2.0f);
            quint16 rawVal = (quint16)((val / 100.0f) * 65535.0f);
            if (i < 128) raw.values[i] = rawVal;
        }
        m_sink->enqueue(raw);
    }
}
