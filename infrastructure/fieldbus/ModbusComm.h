#pragma once
#include <qobject.h>
#include <QTimer>
#include <QQueue>
#include <QMutex>
#include <QAtomicInt>

typedef struct _modbus modbus_t;

struct ModbusConfig {
    enum ConnectionType { Serial, Tcp };
    ConnectionType type = Tcp;
    QString host = "127.0.0.1";
    int port = 502;
    QString portName = "COM1";
    int baudRate = 9600;
    char parity = 'N';
    int dataBit = 8;
    int stopBit = 1;
    int timeout = 1000;
    int retries = 3;
    int poolInterval = 500;
    int heartbeatInterval = 5000;
};

class ModbusComm : public QObject {
    Q_OBJECT
public:
    explicit ModbusComm(QObject* parent = nullptr);
    ~ModbusComm();

    bool connectToHost(const ModbusConfig& config);
    void disconnect();
    bool isConnected() const;

    bool readHoldingRegisters(int serverAddress, int startAddress, int count, QVector<quint16>& values);
    bool readInputRegisters(int serverAddress, int startAddress, int count, QVector<quint16>& values);
    bool writeHoldingRegister(int serverAddress, int address, quint16 value);
    bool writeHoldingRegisters(int serverAddress, int address, const QVector<quint16>& values);

    void setPollConfig(int serverAddress, int startAddress, int count);
    void startPoll();
    void stopPoll();
    bool isPolling() const;

signals:
    void connectionEstablished();
    void connectionLost();
    void connectionError(const QString& error);
    void dataReceived(int serverAddress, int startAddress, const QVector<quint16>& values);
    void writeCompleted(int serverAddress, int address, bool success);
    void heartbeatTimeout();

private:
    void onPollTimeout();
    void onHeartbeatTimeout();
    bool createContext(const ModbusConfig& config);
    void destroyContext();
    void processWriteQueue();
    void attemptReconnect();

    modbus_t* m_ctx = nullptr;
    ModbusConfig m_config;
    QTimer* m_pollTimer = nullptr;
    QTimer* m_heartbeatTimer = nullptr;
    QAtomicInt m_connected;
    QAtomicInt m_polling;
    int m_pollServerAddress = 1;
    int m_poolStartAddress = 0;
    int m_poolCount = 10;
    int m_heartbeatFailCount = 0;
    static constexpr int HEARTBEAT_MAX_FAIL = 3;

    struct WriteTask {
        int serverAddress;
        int address;
        QVector<quint16> values;
    };
    QQueue<WriteTask> m_writeQueue;
    QMutex m_writeMutex;
    bool m_writeInProgress = false;
    QAtomicInt m_reconnecting;
};
