#pragma once
#include "infrastructure/fieldbus/IFieldbus.h"
#include "infrastructure/fieldbus/ModbusComm.h"
#include "infrastructure/messaging/IMessageBus.h"
#include <qobject.h>
#include <qvector.h>
#include <qhash.h>
#include <memory>

class ModbusImpl : public QObject, public IFieldbus {
    Q_OBJECT
public:
    explicit ModbusImpl(QObject* parent = nullptr);
    ~ModbusImpl();

    void addDevice(const DeviceConfig& cfg) override;
    void startAll() override;
    void stopAll() override;
    void writeRegister(int devId, int addr, quint16 val) override;
    void setDataSink(IMessageBus* sink) override;
    bool isDeviceConnected(int devId) const override;
    int onlineDeviceCount() const override;
    int totalDeviceCount() const override;

signals:
    void deviceOnline(int id);
    void deviceOffline(int id);
    void allDevicesOffline();

private:
    void onDataReceived(int serverAddr, int startAddr, const QVector<quint16>& values);
    void checkAllOffline();

    struct DeviceEntry {
        DeviceConfig config;
        ModbusComm* comm = nullptr;
        QThread* thread = nullptr;
    };
    QVector<DeviceEntry> m_devices;
    IMessageBus* m_sink = nullptr;
    int m_failCount = 0;
};
