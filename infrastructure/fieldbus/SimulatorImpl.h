#pragma once
#include "infrastructure/fieldbus/IFieldbus.h"
#include <qobject.h>
#include <qtimer.h>
#include <qvector.h>
#include <qhash.h>
#include <qmath.h>

class SimulatorImpl : public QObject, public IFieldbus {
    Q_OBJECT
public:
    explicit SimulatorImpl(QObject* parent = nullptr);
    ~SimulatorImpl();

    void addDevice(const DeviceConfig& cfg) override;
    void startAll() override;
    void stopAll() override;
    void writeRegister(int devId, int addr, quint16 val) override;
    void setDataSink(IMessageBus* sink) override;
    bool isDeviceConnected(int devId) const override { return m_running; }
    int onlineDeviceCount() const override { return m_running ? m_devices.size() : 0; }
    int totalDeviceCount() const override { return m_devices.size(); }

private:
    void onTick();

    struct SimDevice {
        DeviceConfig config;
        QVector<QPair<int, float>> tagPhases;
    };
    QVector<SimDevice> m_devices;
    QTimer m_timer;
    IMessageBus* m_sink = nullptr;
    bool m_running = false;
    int m_tick = 0;
};
