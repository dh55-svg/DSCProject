#pragma once
#include <qvector.h>
#include <qstring.h>
#include "domain/tag/TagInfo.h"

struct DeviceConfig {
    int deviceId = 0;
    int serverAddr = 1;
    QString ip;
    int port = 502;
    int pollIntervalMs = 500;
    int slaveId = 1;
    int regStart = 0;
    int regCount = 128;
};

class IMessageBus;

class IFieldbus {
public:
    virtual ~IFieldbus() = default;
    virtual void addDevice(const DeviceConfig& cfg) = 0;
    virtual void startAll() = 0;
    virtual void stopAll() = 0;
    virtual void writeRegister(int devId, int addr, quint16 val) = 0;
    virtual void setDataSink(IMessageBus* sink) = 0;
    virtual bool isDeviceConnected(int devId) const = 0;
    virtual int onlineDeviceCount() const = 0;
    virtual int totalDeviceCount() const = 0;
};
