#pragma once

#include "comm_global.h"
#include <qobject.h>
#include <QTimer>
#include <QMap>
#include <QQueue>
#include <QMutex>
#include <QAtomicInt>

typedef struct _modbus modbus_t;
/**
 * @brief Modbus通信配置
 *
 * 工业现场Modbus配置注意事项：
 * - TCP模式：中控室到PLC控制柜通常用以太网，502端口
 * - RTU模式：远端从站可能走串口RS485，9600/8/N/1
 * - 超时时间：工业网络延迟大，至少1000ms
 * - 轮询间隔：化工过程变化慢，500ms~1000ms足够
 */

struct ModbusConfig {
	enum ConnectionType {Serial,Tcp};
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
class COMM_EXPORT ModbusComm :public QObject {
	Q_OBJECT
public:
	explicit ModbusComm(QObject* parent = nullptr);
	~ModbusComm();

	// 连接/断开
	bool connectToHost(const ModbusConfig& config);
	void disconnect();
	bool isConnected() const;

	// 同步读取寄存器（在当前线程中阻塞调用）
	// [L1] 轮询线程中调用，结果通过信号发射
	bool readHoldingRegisters(int serverAddress, int startAddress,
		int count, QVector<quint16>& values);
	bool readInputRegisters(int serverAddress, int startAddress,
		int count, QVector<quint16>& values);

	// 写入寄存器（异步，写入队列）
	bool writeHoldingRegister(int serverAddress, int address, quint16 value);
	bool writeHoldingRegisters(int serverAddress, int address,
		const QVector<quint16>& values);

	// 轮询配置
	void setPollConfig(int serverAddress, int startAddress, int count);
	void startPoll();
	void stopPoll();
	bool isPolling() const;

signals:
	// 连接状态信号
	void connectionEstablished();
	void connectionLost();
	void connectionError(const QString& error);

	// [L1->L2] 数据接收信号（核心信号，写入无锁队列）
	void dataReceived(int serverAddress, int startAddress,
		const QVector<quint16>& values);

	// 写入完成信号
	void writeCompleted(int serverAddress, int address, bool success);

	// 心跳超时信号
	void heartbeatTimeout();
private :
	void onPollTimeout();
	void onHeartbeatTimeout();
private:
	bool createContext(const ModbusConfig& config);
	void destoryContext();
	// [L1] 处理写入队列
	void processWriteQueue();
	// [L1] 自动重连
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

	// 心跳计数器
	int m_heartbeatFailCount = 0;
	static constexpr int HEARTBEAT_MAX_FAIL = 3;

	struct WriteTask {
		int serverAddress;
		int address;
		QVector<quint16> values;
	};

	QQueue< WriteTask> m_writeQueue;
	QMutex m_writeMutex;
	bool m_writeInProgress = false;

	// 重连控制
	QAtomicInt m_reconnectiong;

};
