#pragma once
#include "export.h"
#include <qthread.h>
#include <QAtomicInt>
#include <qhash.h>
#include <QVector>
#include <QDateTime>
#include "TagDef.h"
#include "lockFreeRingBuffer.h"
#include "DoubleBuffer.h"
#include "ModbusManager.h"
/**
 * @brief 数据解析线程
 *
 * 这是改进架构的核心组件，独立线程运行，负责：
 * 1. 从无锁循环队列中读取多个Modbus设备的原始数据
 * 2. 将寄存器原始值转换为工程值（量程转换）
 * 3. 执行报警判断（含死区处理）
 * 4. 变化率校验（防跳变）
 * 5. 将处理后的数据写入双缓冲区
 * 6. 定期交换双缓冲区，让UI线程读取最新数据
 *
 * 数据流：
 * ┌─────────────────┐     ┌──────────────────┐     ┌──────────────┐
 * │ LockFreeRingBuf │────▶│  DataParseThread │────▶│ DoubleBuffer │
 * │ (多设备原始数据) │     │  解析+报警判断   │     │ (双缓冲区)   │
 * └─────────────────┘     └──────────────────┘     └──────┬───────┘
 *                                                         │ swap
 *                                                         ▼
 *                                                  ┌──────────────┐
 *                                                  │  UI Thread   │
 *                                                  │  读取显示    │
 *                                                  └──────────────┘
 *
 * 线程模型：
 * - DataParseThread是唯一消费者（单线程dequeue，无竞争）
 * - 写入双缓冲区时不需要锁，因为写的是写缓冲区，UI读的是读缓冲区
 * - swap操作是原子的，瞬间完成
 *
 * 频率解耦：
 * - Modbus采集频率：100ms~1000ms（各设备独立）
 * - DataParseThread处理频率：10ms~50ms（可配置）
 * - 双缓冲区swap频率：50ms~100ms（可配置）
 * - UI刷新频率：100ms~200ms（由UI定时器控制）
 *
 * 踩坑经验：
 * - 之前把数据解析放在主线程，300个点每500ms更新一次，
 *   主线程CPU占用30%，UI明显卡顿
 * - 独立线程后主线程CPU降到5%以下
 * - 批量处理比逐条处理效率高10倍以上
 * - swap频率不能太高，否则UI频繁重绘反而更卡
 */

class DATAENGINE_EXPORT DataParseThread :public QThread {
	Q_OBJECT
public:
	explicit DataParseThread(QObject* parent = nullptr);
	~DataParseThread() override;

	/**
	 * @brief 设置无锁循环队列（数据源）
	 */
	void setRingBuffer(LockFreeRingBuffer<RawModbusData, 8192>* queue);

	/**
	 * @brief 设置双缓冲区（数据目标）
	 */
	void setDoubleBuffer(DoubleBuffer* buffer);

	/**
	 * @brief 加载位号配置（用于寄存器到工程值的转换）
	 *
	 * @param tags 位号列表
	 */

	void setTagConfig(const QVector<TagInfo>& tags);

	/**
	 * @brief 设置处理间隔（毫秒）
	 *
	 * 控制从队列读取数据的频率。
	 * 设太小浪费CPU，设太大数据延迟大。
	 * 推荐：10ms~50ms
	 */
	void setProcessInterval(int ms);

	/**
	 * @brief 设置双缓冲区交换间隔（毫秒）
	 *
	 * 控制UI看到新数据的频率。
	 * 推荐：50ms~100ms
	 */
	void setSwapInterval(int ms);

	/**
	 * @brief 停止线程
	 */
	void stop();

	/**
	 * @brief 获取处理统计信息
	 */
	qint64 totalProcessed() const { return m_totalProcessed.loadRelaxed(); }
	qint64 totalAlarms() const { return m_totalAlarms.loadRelaxed(); }
	qint64 totalJumpDetected() const { return m_totalJumpDetected.loadRelaxed(); }

signals:
	/**
	 * @brief 数据更新完成信号（通知UI可以刷新）
	 *
	 * 每次swap双缓冲区后发射此信号。
	 * UI层连接此信号来触发界面刷新。
	 */
	void dataUpdated();

	/**
	 * @brief 报警触发信号
	 */
		void alarmTriggered(quint32 tagId, AlarmLimit state, float value, float limit);

	/**
	 * @brief 报警恢复信号
	 */
	void alarmCleared(quint32 tagId);

	/**
	 * @brief 设备断线信号（所有位号质量码标记为Bad）
	 */
	void deviceOffline(int deviceId);

protected:
	void run() override;
private:
	/**
	 * @brief 处理一批原始数据
	 */
	void processBatch(const std::vector<RawModbusData>& batch);

	/**
	 * @brief 将Modbus寄存器值转换为工程值
	 */
	float registerToValue(quint16 rawValue, const TagInfo& tag) const;

	/**
	 * @brief 报警判断（含死区处理）
	 */
	void checkAlarm(quint32 tagId, float value);

	/**
	 * @brief 优化的报警判断（直接传入TagInfo，避免重复查找）
	 *
	 * ISA-18.2 商业化增强版，覆盖6种报警类型：
	 * 1. HighHigh / High / Low / LowLow 限值报警
	 * 2. 偏差报警（|PV-SP| > deviationLimit）
	 * 3. 变化率报警（值变化速率 > rateOfChangeLimit）
	 */
	void checkAlarmOptimized(const TagInfo& tag, float value);

	/**
	 * @brief 偏差报警检查（ISA-18.2 Deviation Alarm）
	 *
	 * 当实际值与设定值偏差超过限值时触发。
	 * 典型场景：PID控制回路输出偏差过大。
	 *
	 * @param tag   位号信息（含 deviationLimit, deviationEnabled, setPoint）
	 * @param value 当前过程值
	 */
	void checkDeviationAlarm(const TagInfo& tag, float value);

	/**
	 * @brief 变化率报警检查（ISA-18.2 Rate-of-Change Alarm）
	 *
	 * 当值的变化速率超过限值时触发。
	 * 典型场景：温度骤升、压力突降等异常工况。
	 *
	 * 注意：此方法与 validateRateOfChange 不同：
	 * - validateRateOfChange: 数据质量校验，过滤噪声/跳变
	 * - checkRateOfChangeAlarm: 工艺报警，检测真实异常变化率
	 *
	 * @param tag       位号信息（含 rateOfChangeLimit, rateOfChangeEnabled, rateOfChangePeriodMs）
	 * @param value     当前过程值
	 * @param timestamp 当前时间戳
	 */
	void checkRateOfChangeAlarm(const TagInfo& tag, float value, qint64 timestamp);

	/**
	 * @brief 处理设备断线（将该设备所有位号质量码标记为Bad）
	 */
	void markDeviceBad(int deviceId);

	/**
	 * @brief 变化率校验，检测异常跳变（数据质量校验，非报警）
	 * @return true 通过校验，false 异常跳变
	 */
	bool validateRateOfChange(quint32 tagId, float value, const TagInfo& tag,
	                          qint64 timestamp, DataQuality& outQuality);

	// 外部组件引用（不拥有）
	LockFreeRingBuffer<RawModbusData, 8192>* m_ringBuffer = nullptr;
	DoubleBuffer* m_doubleBuffer = nullptr;

	// 位号配置（按serverAddress+regAddr索引，加速查找）
	QHash<quint64, TagInfo> m_tagByRegAddr;  // key = (serverAddr << 32) | regAddr 按寄存器地址索引（用于快速查找位号信息）
	QHash<int, QVector<quint32>> m_tagsByDevice;  // deviceId -> tagId列表 按设备地址索引（用于设备管理和批量操作）

	// 运行控制
	QAtomicInt m_running;
	int m_processInterval = 20;   // 处理间隔（毫秒）设置从队列中读取和处理数据的间隔时间（毫秒）
	int m_swapInterval = 50;      // 双缓冲区交换间隔（毫秒）

	// 报警死区状态记录
	QHash<quint32, bool> m_inDeadband;

	// 偏差报警状态记录（跟踪偏差报警是否已触发）
	QHash<quint32, bool> m_deviationAlarmActive;

	// 变化率报警状态记录（跟踪变化率报警是否已触发）
	struct RateOfChangeAlarmState {
		float   lastValue = 0.0f;          // 上一次采样值
		qint64  lastTimestamp = 0;          // 上一次采样时间戳
		bool    alarmActive = false;        // 变化率报警是否活跃
		float   peakRate = 0.0f;           // 周期内峰值变化率
	};
	QHash<quint32, RateOfChangeAlarmState> m_rocAlarmState;

	// 变化率校验（防跳变，数据质量用）
	struct RocEntry {
		float lastValidValue = 0.0f;
		qint64 lastTimestamp = 0;
	};
	QHash<quint32, RocEntry> m_rocState;

	// 统计
	QAtomicInt m_totalProcessed;
	QAtomicInt m_totalAlarms;
	QAtomicInt m_totalJumpDetected;
	QAtomicInt m_totalDeviationAlarms;     // 偏差报警计数
	QAtomicInt m_totalRocAlarms;           // 变化率报警计数
};
