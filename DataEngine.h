#pragma once
#include "export.h"
#include <QObject>
#include <QTimer>
#include <QVector>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QFile>
#include "TagDef.h"
#include "RealtimeDb.h"
#include "lockFreeRingBuffer.h"
#include "DoubleBuffer.h"
#include "ModbusManager.h"
#include "DataParseThread.h"
/**
 * @brief DCS数据引擎核心（改进版）
 *
 * 改进后的架构：
 * ┌──────────────────────────────────────────────────────────────────────┐
 * │                        DataEngine (调度中枢)                         │
 * │                                                                      │
 * │  ┌────────────┐  ┌──────────────┐  ┌────────────┐  ┌────────────┐  │
 * │  │ModbusManager│  │LockFreeQueue │  │DataParse   │  │DoubleBuffer│  │
 * │  │多设备采集   │─▶│无锁循环队列  │─▶│Thread      │─▶│双缓冲区    │  │
 * │  │            │  │              │  │解析+报警   │  │            │  │
 * │  └────────────┘  └──────────────┘  └────────────┘  └─────┬──────┘  │
 * │                                                          │          │
 * │                    ┌────────────┐                         │          │
 * │                    │UI Thread   │◀───swap────────────────┘          │
 * │                    │界面刷新    │                                    │
 * │                    └────────────┘                                    │
 * └──────────────────────────────────────────────────────────────────────┘
 *
 * 改进要点：
 * 1. 多设备：ModbusManager管理多个ModbusComm实例
 * 2. 无锁队列：MPSC无锁循环队列替代信号槽，减少跨线程开销
 * 3. 独立解析线程：DataParseThread负责数据解析和报警判断
 * 4. 双缓冲区：读写频率解耦，UI线程无锁读取
 * 5. 纯内存存储：实时数据使用QHash + QReadWriteLock，零延迟读写
 *
 * 踩坑经验：
 * - DataEngine本身运行在主线程，但不再做数据处理
 * - 数据处理全部在DataParseThread中完成
 * - DataEngine只负责初始化、配置加载、生命周期管理
 */
class DATAENGINE_EXPORT DataEngine :public QObject {
	Q_OBJECT
public:
	explicit DataEngine(QObject *parent=nullptr);
	~DataEngine() override;
    /**
     * @brief 初始化数据引擎（创建所有组件并连接）
     *
     * 初始化顺序很重要：
     * 1. 创建无锁循环队列
     * 2. 创建双缓冲区
     * 3. 创建ModbusManager（绑定队列）
     * 4. 创建DataParseThread（绑定队列+缓冲区）
     * 5. 创建HistoryArchiveThread（绑定缓冲区）
     */
    bool initialize();

    /**
     * @brief 添加Modbus设备
     */
    bool addModbusDevice(const ModbusDeviceConfig& config);

    /**
     * @brief 加载位号配置（从JSON文件）
     */
    bool loadTagConfig(const QString& jsonPath);

    /**
     * @brief 启动数据引擎
     */
    void start();

    /**
     * @brief 停止数据引擎
     */
    void stop();

    /**
     * @brief 是否正在运行
     */
    bool isRunning() const;

    /**
     * @brief 操作员下发：设置SP
     */
    void setSetPoint(quint32 tagId, float sp);

    /**
     * @brief 操作员下发：设置OUT
     */
    void setOutput(quint32 tagId, float out);

    /**
     * @brief 操作员下发：切换自动/手动模式
     */
    void setAutoMode(quint32 tagId, bool autoMode);

    /**
     * @brief 获取双缓冲区引用（UI层用于读取数据）
     */
    DoubleBuffer* doubleBuffer() { return &m_doubleBuffer; }

    /**
     * @brief 获取ModbusManager引用
     */
    ModbusManager* modbusManager() { return m_modbusManager; }

    /**
     * @brief 获取实时数据库引用（兼容旧接口）
     */
    RealtimeDb& realtimeDb() { return RealtimeDb::instance(); }

signals:
    /**
     * @brief 数据更新信号（DataParseThread swap后发射）
     */
    void dataUpdated();

    /**
     * @brief 报警触发信号
     */
    void alarmTriggered(quint32 tagId, AlarmState state, float value, float limit);

    /**
     * @brief 报警恢复信号
     */
    void alarmCleared(quint32 tagId);

    /**
     * @brief 通信状态变化信号
     */
    void commStatusChanged(bool connected);

    /**
     * @brief 设备状态变化信号
     */
    void deviceStatusChanged(int deviceId, bool connected);

    /**
     * @brief MySQL归档完成信号（已弃用，纯内存存储）
     */
    void archiveCompleted(int recordCount, qint64 durationMs);

    /**
     * @brief MySQL归档失败信号（已弃用，纯内存存储）
     */
    void archiveFailed(const QString& error);

private slots:
    /**
     * @brief 处理数据更新（从DataParseThread转发）
     */
    void onDataUpdated();

    /**
     * @brief 处理所有设备离线
     */
    void onAllDevicesOffline();
private:
    LockFreeRingBuffer<RawModbusData, 8192> m_ringBuffer;
    DoubleBuffer m_doubleBuffer;
    ModbusManager* m_modbusManager = nullptr;
    DataParseThread* m_dataParseThread = nullptr;

    bool m_running = false;                                 // 运行标志
    bool m_initialized = false;                             // 初始化标志
};

