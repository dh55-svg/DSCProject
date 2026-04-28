#pragma once
#include "infrastructure/fieldbus/IFieldbus.h"

// Placeholder for future OPC UA implementation
class OpcUaImpl : public IFieldbus {
public:
    void addDevice(const DeviceConfig& cfg) override { Q_UNUSED(cfg); }
    void startAll() override {}
    void stopAll() override {}
    void writeRegister(int devId, int addr, quint16 val) override { Q_UNUSED(devId); Q_UNUSED(addr); Q_UNUSED(val); }
    void setDataSink(IMessageBus* sink) override { Q_UNUSED(sink); }
    bool isDeviceConnected(int) const override { return false; }
    int onlineDeviceCount() const override { return 0; }
    int totalDeviceCount() const override { return 0; }
};
