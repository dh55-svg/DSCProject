#pragma once
#include "comm_global.h"
#include "ModbusComm.h"
#include "lockFreeRingBuffer.h"
#include <QObject>
#include <QVector>
#include <QHash>
#include <QThread>
#include <QTimer>
#include <QAtomicInt>
/**
 * @brief Modbus采集到的原始数据帧
 *
 * [L1->L2] 从无锁循环队列中传递的数据结构
 * Modbus采集线程写入，DataParseThread读取
 *
 * 设计要点：
 * - 必须是POD-like类型，可以安全地做memcpy
 * - 不包含QString等需要堆分配的类型，避免无锁队列中的内存问题
 * - values数组大小128，足够覆盖单次Modbus读取（最大125个寄存器）
 */
struct RawModbusData {
    int deviceId = 0;               // 设备编号（ModbusManager中分配）
    int serverAddress = 0;          // Modbus从站地址
    int startAddress = 0;           // 寄存器起始地址
    qint64 timestamp = 0;           // 采集时间戳（毫秒，L1层锚定）
    int valueCount = 0;             // 有效数据数量
    quint16 values[128] = { 0 };      // 寄存器原始值（最多128个寄存器）
};
/**
 * @brief 设备配置信息
 *
 * 支持12台PLC，每台PLC可以有不同的IP和从站地址
 */
struct ModbusDeviceConfig {
    int deviceId = 0;
    ModbusConfig modbusConfig;
    int pollServerAddress = 1;       // 轮询从站地址
    int pollStartAddress = 0;        // 轮询起始寄存器
    int pollCount = 10;              // 轮询寄存器数量
    QString deviceName;              // 设备名称，如"1号反应釜PLC"
};

class COMM_EXPORT ModbusManager :public QObject {
    Q_OBJECT
public:
    explicit ModbusManager(QObject* parent=nullptr);
    ~ModbusManager();
    // [L1] 设置无锁循环队列（必须在addDevice之前调用）
    void setRingBuffer(LockFreeRingBuffer<RawModbusData, 8192>* queue);

    // [L1] 添加Modbus设备（最多12台PLC）
    bool addDevice(const ModbusDeviceConfig& config);

    // [L1] 移除Modbus设备
    bool removeDevice(int deviceId);

    // [L1] 批量启动/停止
    void startAll();
    void stopAll();

    // [L1] 单设备启动/停止
    bool startDevice(int deviceId);
    bool stopDevice(int deviceId);

    // [L1] 设备状态查询
    bool isDeviceConnected(int deviceId) const;
    QVector<int> deviceIds() const;
    int onlineDeviceCount() const;
    int totalDeviceCount() const;

    // [L1] 写入寄存器（操作员下发用，独立指令通道）
    bool writeRegister(int deviceId, int serverAddress,
        int address, quint16 value);

signals:
    // [L1->L5] 设备连接状态变化信号（穿透到UI层）
    void deviceStatusChanged(int deviceId, bool connected);
    void allDevicesOffline();

private slots:
    // [L1] 处理Modbus数据接收（将数据写入无锁队列）
    // DirectConnection：在ModbusComm线程中执行，仅做数据打包+入队
    void onDataReceived(int serverAddress, int startAddress,
        const QVector<quint16>& values);

    void onDeviceConnectionLost();
    void onDeviceConnectionEstablished();
private:
    // 设备上下文：每个设备独立的ModbusComm和线程
    struct DeviceConfig {
        int deviceId = 0;
        ModbusComm* comm = nullptr;
        QThread* thread = nullptr;
        ModbusDeviceConfig config;
        bool connected = false;// 连接状态
    };
    QHash<int, DeviceConfig> m_device;
    LockFreeRingBuffer<RawModbusData, 8192>* m_ringBuffer = nullptr;
    QAtomicInt m_running;
    mutable QMutex m_devicemutex;

    QAtomicInt m_totalEnqueued;
    QAtomicInt m_totalDropped;

};
