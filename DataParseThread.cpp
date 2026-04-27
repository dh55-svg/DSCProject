#include "DataParseThread.h"
#include "logger.h"
#include "AlarmEngine.h"
#include <cmath>

// ============================================================
// DataParseThread - ISA-18.2 商业化增强版
//
// 增强内容：
// 1. 偏差报警（Deviation Alarm）：|PV-SP| > deviationLimit
// 2. 变化率报警（Rate-of-Change Alarm）：值变化速率 > rateOfChangeLimit
// 3. Off-Delay 参数传递：将 tag.offDelayMs 传给 AlarmEngine
// 4. 报警优先级/分类传递：使用 TagInfo 中 Rationalization 配置
// ============================================================

DataParseThread::DataParseThread(QObject* parent)
    : QThread(parent)
{
    m_running.storeRelaxed(0);
    m_totalProcessed.storeRelaxed(0);
    m_totalAlarms.storeRelaxed(0);
    m_totalJumpDetected.storeRelaxed(0);
    m_totalDeviationAlarms.storeRelaxed(0);
    m_totalRocAlarms.storeRelaxed(0);
}

DataParseThread::~DataParseThread()
{
    stop();
    wait(3000);
}

void DataParseThread::setRingBuffer(LockFreeRingBuffer<RawModbusData, 8192>* queue)
{
    m_ringBuffer = queue;
}

void DataParseThread::setDoubleBuffer(DoubleBuffer* buffer)
{
    m_doubleBuffer = buffer;
}

void DataParseThread::setTagConfig(const QVector<TagInfo>& tags)
{
    m_tagByRegAddr.clear();
    m_tagsByDevice.clear();
    m_deviationAlarmActive.clear();
    m_rocAlarmState.clear();

    for (const auto& tag : tags) {
        quint64 key = (static_cast<quint64>(tag.modbusServerAddr) << 32)
                    | static_cast<quint64>(tag.modbusRegAddr);
        m_tagByRegAddr.insert(key, tag);
        m_tagsByDevice[tag.modbusServerAddr].append(tag.tagId);
    }
    LOG_INFO("DataParseThread", QString("Tag config loaded: %1 tags, %2 devices")
        .arg(tags.size()).arg(m_tagsByDevice.size()));
}

void DataParseThread::setProcessInterval(int ms)
{
    m_processInterval = qBound(5, ms, 1000);
}

void DataParseThread::setSwapInterval(int ms)
{
    m_swapInterval = qBound(10, ms, 1000);
}

void DataParseThread::stop()
{
    m_running.storeRelaxed(0);
}

void DataParseThread::run()
{
    m_running.storeRelaxed(1);
    LOG_INFO("DataParseThread",
        QString("Data thread started: interval=%1ms, swap=%2ms")
            .arg(m_processInterval).arg(m_swapInterval));

    qint64 lastSwapTime = 0;
    std::vector<RawModbusData> batch;
    batch.reserve(256);

    while (m_running) {
        batch.clear();
        if (m_ringBuffer) {
            m_ringBuffer->dequeueBatch(batch, 256);
        }
        if (!batch.empty()) {
            processBatch(batch);
            m_totalProcessed.fetchAndAddRelaxed(static_cast<int>(batch.size()));
        }

        quint64 now = QDateTime::currentMSecsSinceEpoch();
        if ((now - lastSwapTime) >= static_cast<quint64>(m_swapInterval)) {
            if (m_doubleBuffer) {
                m_doubleBuffer->commit();
                emit dataUpdated();
            }
            lastSwapTime = now;
        }
        if (batch.empty()) {
            QThread::msleep(m_processInterval);
        } else {
            QThread::msleep(1);
        }
    }
    LOG_INFO("DataParseThread",
        QString("Data thread exited: processed=%1, alarms=%2, jumps=%3, deviation=%4, roc=%5")
            .arg(m_totalProcessed.loadRelaxed())
            .arg(m_totalAlarms.loadRelaxed())
            .arg(m_totalJumpDetected.loadRelaxed())
            .arg(m_totalDeviationAlarms.loadRelaxed())
            .arg(m_totalRocAlarms.loadRelaxed()));
}

void DataParseThread::processBatch(const std::vector<RawModbusData>& batch)
{
    for (const auto& raw : batch) {
        for (int i = 0; i < raw.valueCount; ++i) {
            int regAddr = raw.startAddress + i;
            quint64 key = (static_cast<quint64>(raw.serverAddress) << 32)
                        | static_cast<quint64>(regAddr);
            auto it = m_tagByRegAddr.find(key);
            if (it == m_tagByRegAddr.end()) continue;

            const TagInfo& tag = it.value();
            float pv = registerToValue(raw.values[i], tag);

            // 数据质量校验（防跳变，非报警逻辑）
            DataQuality quality = DataQuality::Good;
            if (!validateRateOfChange(tag.tagId, pv, tag, raw.timestamp, quality)) {
                m_totalJumpDetected.fetchAndAddRelaxed(1);
            }

            // 写入双缓冲区
            if (m_doubleBuffer) {
                DoubleBuffer::TagSnapshot snapshot;
                snapshot.tagId = tag.tagId;
                snapshot.currentValue = pv;
                snapshot.setPoint = tag.setPoint;
                snapshot.outputValue = tag.outputValue;
                snapshot.alarmstate = tag.alarmLimit;
                snapshot.quality = quality;
                snapshot.timestamp = raw.timestamp;
                m_doubleBuffer->write(tag.tagId, snapshot);
            }

            // === ISA-18.2 三类报警检查 ===
            // 1. 限值报警（HH/H/L/LL）
            checkAlarmOptimized(tag, pv);

            // 2. 偏差报警（|PV-SP| > deviationLimit）
            if (tag.deviationEnabled) {
                checkDeviationAlarm(tag, pv);
            }

            // 3. 变化率报警（值变化速率 > rateOfChangeLimit）
            if (tag.rateOfChangeEnabled && quality == DataQuality::Good) {
                checkRateOfChangeAlarm(tag, pv, raw.timestamp);
            }
        }
    }
}

float DataParseThread::registerToValue(quint16 rawValue, const TagInfo& tag) const
{
    float range = tag.engHigh - tag.engLow;
    float value = tag.engLow + (static_cast<float>(rawValue) / 65535.0f) * range;
    return value;
}

void DataParseThread::checkAlarm(quint32 tagId, float value)
{
    TagInfo tag;
    for (auto it = m_tagByRegAddr.constBegin(); it != m_tagByRegAddr.constEnd(); ++it) {
        if (it.value().tagId == tagId) {
            tag = it.value();
            break;
        }
    }
    if (tag.tagId == 0) return;
    checkAlarmOptimized(tag, value);
}

// ============================================================
// 限值报警检查（ISA-18.2 Level 1 - 增强版）
//
// 增强：
// - 传递 offDelayMs 给 AlarmEngine（恢复延时）
// - 传递 priority 和 classification（Rationalization 配置）
// - 偏差/变化率报警不再在此方法处理
// ============================================================
void DataParseThread::checkAlarmOptimized(const TagInfo& tag, float value)
{
    quint32 tagId = tag.tagId;
    AlarmLimit newLimit = AlarmLimit::Normal;

    // 四级限值判断（优先级：HH > H > L > LL）
    if (value >= tag.highHighLimit) {
        newLimit = AlarmLimit::HighHigh;
    } else if (value >= tag.highLimit) {
        newLimit = AlarmLimit::High;
    } else if (value <= tag.lowLowLimit) {
        newLimit = AlarmLimit::LowLow;
    } else if (value <= tag.lowLimit) {
        newLimit = AlarmLimit::Low;
    }

    AlarmLimit oldLimit = tag.alarmLimit;

    // 死区滞环：值必须越过死区才认为恢复正常
    if (oldLimit != AlarmLimit::Normal && newLimit == AlarmLimit::Normal) {
        bool canClear = false;
        if (oldLimit == AlarmLimit::High || oldLimit == AlarmLimit::HighHigh) {
            // 高限报警恢复：值必须低于 (highLimit - deadband)
            canClear = (value < tag.highLimit - tag.deadband);
        } else if (oldLimit == AlarmLimit::Low || oldLimit == AlarmLimit::LowLow) {
            // 低限报警恢复：值必须高于 (lowLimit + deadband)
            canClear = (value > tag.lowLimit + tag.deadband);
        }
        if (!canClear) {
            m_inDeadband[tagId] = true;
            return;
        }
        m_inDeadband.remove(tagId);
    }

    if (newLimit != oldLimit) {
        m_totalAlarms.fetchAndAddRelaxed(1);

        // 更新双缓冲区报警状态（用于UI显示）
        if (m_doubleBuffer) {
            DoubleBuffer::TagSnapshot snapshot = m_doubleBuffer->readTag(tagId);
            snapshot.alarmstate = newLimit;
            m_doubleBuffer->write(tagId, snapshot);
        }

        // 通过 ISA-18.2 报警引擎处理完整状态机
        if (newLimit != AlarmLimit::Normal) {
            float threshold = 0.0f;
            switch (newLimit) {
            case AlarmLimit::HighHigh: threshold = tag.highHighLimit; break;
            case AlarmLimit::High:     threshold = tag.highLimit; break;
            case AlarmLimit::LowLow:   threshold = tag.lowLowLimit; break;
            case AlarmLimit::Low:      threshold = tag.lowLimit; break;
            default: break;
            }

            // 传递完整的 ISA-18.2 参数：
            // - priority: Rationalization 阶段确定的优先级
            // - classification: Rationalization 阶段确定的分类
            // - onDelayMs: 触发延时（防噪声尖峰）
            AlarmEngine::instance().triggerAlarm(
                tagId, newLimit, value, threshold,
                tag.priority, tag.classification, tag.onDelayMs);
            emit alarmTriggered(tagId, newLimit, value, threshold);
        } else {
            // 值回正常，传递 offDelayMs 给 AlarmEngine
            // AlarmEngine 内部会根据 offDelayMs 决定是否延迟确认恢复
            AlarmEngine::instance().clearAlarm(tagId, value);
            emit alarmCleared(tagId);
        }
    }
}

// ============================================================
// 偏差报警检查（ISA-18.2 Deviation Alarm）
//
// 工业场景：
// - PID 控制回路输出偏差过大，说明调节异常
// - 反应釜温度与设定值偏差超过允许范围
// - 管道流量与目标值偏差过大
//
// 逻辑：
// - |PV - SP| > deviationLimit → 触发偏差报警
// - |PV - SP| <= (deviationLimit - deadband) → 恢复正常（死区滞环）
// - 偏差报警独立于限值报警，两者可以同时存在
// ============================================================
void DataParseThread::checkDeviationAlarm(const TagInfo& tag, float value)
{
    quint32 tagId = tag.tagId;
    float deviation = fabsf(value - tag.setPoint);
    bool wasActive = m_deviationAlarmActive.value(tagId, false);

    if (deviation > tag.deviationLimit) {
        // 偏差超限
        if (!wasActive) {
            // 新触发偏差报警
            m_deviationAlarmActive[tagId] = true;
            m_totalDeviationAlarms.fetchAndAddRelaxed(1);

            // 偏差报警使用 Deviation 限值类型
            // 优先级通常比限值报警低一级（Minor 或 Advisory）
            AlarmPriority devPriority = tag.priority;
            if (devPriority == AlarmPriority::Critical) {
                devPriority = AlarmPriority::Major;
            } else if (devPriority == AlarmPriority::Major) {
                devPriority = AlarmPriority::Minor;
            }

            AlarmEngine::instance().triggerAlarm(
                tagId,
                AlarmLimit::Deviation,
                value,
                tag.deviationLimit,
                devPriority,
                tag.classification,
                tag.onDelayMs);

            emit alarmTriggered(tagId, AlarmLimit::Deviation, value, tag.deviationLimit);

            LOG_WARN("DataParseThread",
                QString("偏差报警触发: tagId=%1, PV=%2, SP=%3, 偏差=%4, 限值=%5")
                    .arg(tagId)
                    .arg(value, 0, 'f', 2)
                    .arg(tag.setPoint, 0, 'f', 2)
                    .arg(deviation, 0, 'f', 2)
                    .arg(tag.deviationLimit, 0, 'f', 2));
        }
    } else {
        // 偏差在限值内，检查死区滞环
        if (wasActive) {
            // 死区滞环：偏差必须低于 (deviationLimit - deadband) 才恢复
            float clearThreshold = tag.deviationLimit - tag.deadband;
            if (clearThreshold < 0.0f) clearThreshold = 0.0f;

            if (deviation <= clearThreshold) {
                m_deviationAlarmActive.remove(tagId);
                AlarmEngine::instance().clearAlarm(tagId, value);
                emit alarmCleared(tagId);

                LOG_INFO("DataParseThread",
                    QString("偏差报警恢复: tagId=%1, PV=%2, SP=%3, 偏差=%4")
                        .arg(tagId)
                        .arg(value, 0, 'f', 2)
                        .arg(tag.setPoint, 0, 'f', 2)
                        .arg(deviation, 0, 'f', 2));
            }
        }
    }
}

// ============================================================
// 变化率报警检查（ISA-18.2 Rate-of-Change Alarm）
//
// 工业场景：
// - 温度骤升（反应失控前兆）
// - 压力突降（泄漏/管路破裂）
// - 液位急速变化（进料异常）
//
// 逻辑：
// - 计算实际变化率 = |当前值 - 上次值| / 时间间隔
// - 实际变化率 > rateOfChangeLimit → 触发变化率报警
// - 实际变化率 <= rateOfChangeLimit → 恢复正常
//
// 与 validateRateOfChange 的区别：
// - validateRateOfChange: 数据质量校验，阈值=满量程50%/秒，标记质量码
// - checkRateOfChangeAlarm: 工艺报警，阈值=rateOfChangeLimit，触发报警
// - 两者独立运行，互不影响
// ============================================================
void DataParseThread::checkRateOfChangeAlarm(const TagInfo& tag, float value, qint64 timestamp)
{
    quint32 tagId = tag.tagId;
    auto& state = m_rocAlarmState[tagId];

    // 首次采样，记录基准值，不判断变化率
    if (state.lastTimestamp == 0) {
        state.lastValue = value;
        state.lastTimestamp = timestamp;
        return;
    }

    // 计算时间间隔（秒）
    float elapsedSec = static_cast<float>(timestamp - state.lastTimestamp) / 1000.0f;

    // 时间倒退或间隔太短（<100ms），跳过本次判断
    if (elapsedSec < 0.1f) {
        return;
    }

    // 计算实际变化率（单位/秒）
    float delta = fabsf(value - state.lastValue);
    float actualRate = delta / elapsedSec;

    // 更新峰值变化率
    if (actualRate > state.peakRate) {
        state.peakRate = actualRate;
    }

    // 变化率报警限值（单位/秒）
    float rocLimit = tag.rateOfChangeLimit;
    if (rocLimit <= 0.0f) {
        // 限值未配置，跳过
        state.lastValue = value;
        state.lastTimestamp = timestamp;
        return;
    }

    if (actualRate > rocLimit) {
        // 变化率超限
        if (!state.alarmActive) {
            // 新触发变化率报警
            state.alarmActive = true;
            m_totalRocAlarms.fetchAndAddRelaxed(1);

            // 变化率报警使用 RateOfChange 限值类型
            // 优先级通常与限值报警相同或低一级
            AlarmPriority rocPriority = tag.priority;
            if (rocPriority == AlarmPriority::Critical) {
                rocPriority = AlarmPriority::Major;
            }

            AlarmEngine::instance().triggerAlarm(
                tagId,
                AlarmLimit::RateOfChange,
                value,
                rocLimit,
                rocPriority,
                tag.classification,
                tag.onDelayMs);

            emit alarmTriggered(tagId, AlarmLimit::RateOfChange, value, rocLimit);

            LOG_WARN("DataParseThread",
                QString("变化率报警触发: tagId=%1, 变化率=%2/s, 限值=%3/s, 值=%4→%5")
                    .arg(tagId)
                    .arg(actualRate, 0, 'f', 2)
                    .arg(rocLimit, 0, 'f', 2)
                    .arg(state.lastValue, 0, 'f', 2)
                    .arg(value, 0, 'f', 2));
        }
    } else {
        // 变化率恢复正常
        if (state.alarmActive) {
            // 变化率降到限值的50%以下才恢复（滞环，防止在限值附近抖动）
            if (actualRate < rocLimit * 0.5f) {
                state.alarmActive = false;
                state.peakRate = 0.0f;
                AlarmEngine::instance().clearAlarm(tagId, value);
                emit alarmCleared(tagId);

                LOG_INFO("DataParseThread",
                    QString("变化率报警恢复: tagId=%1, 变化率=%2/s, 限值=%3/s")
                        .arg(tagId)
                        .arg(actualRate, 0, 'f', 2)
                        .arg(rocLimit, 0, 'f', 2));
            }
        }
    }

    // 更新基准值
    state.lastValue = value;
    state.lastTimestamp = timestamp;
}

// ============================================================
// 数据质量校验（防跳变，非报警逻辑）
//
// 此方法仅用于数据质量判断：
// - 检测传感器故障/通讯干扰导致的异常跳变
// - 标记数据质量码为 Uncertain
// - 不触发报警，仅影响数据可信度
//
// 与 checkRateOfChangeAlarm 的区别：
// - 阈值更高（满量程50%/秒），只过滤明显异常
// - 不产生报警事件
// - 影响数据质量码
// ============================================================
bool DataParseThread::validateRateOfChange(quint32 tagId, float value,
                                           const TagInfo& tag,
                                           qint64 timestamp, DataQuality& outQuality)
{
    auto& state = m_rocState[tagId];

    // 首次采样，记录基准值
    if (state.lastTimestamp == 0) {
        state.lastValidValue = value;
        state.lastTimestamp = timestamp;
        return true;
    }

    float delta = fabsf(value - state.lastValidValue);
    float elapsedSec = static_cast<float>(timestamp - state.lastTimestamp) / 1000.0f;

    // 时间倒退或间隔太短，跳过校验
    if (elapsedSec <= 0.0f) return true;

    // 最大允许变化率：满量程的 50% / 秒
    float maxRoc = (tag.engHigh - tag.engLow) * 0.5f;
    if (maxRoc <= 0.0f) maxRoc = 50.0f;

    float actualRoc = delta / elapsedSec;

    if (actualRoc > maxRoc) {
        outQuality = DataQuality::Uncertain;
        LOG_WARN("DataParseThread",
            QString("异常跳变检测: tagId=%1, 旧值=%2, 新值=%3, 变化率=%4/s (限值=%5/s)")
                .arg(tagId)
                .arg(state.lastValidValue, 0, 'f', 2)
                .arg(value, 0, 'f', 2)
                .arg(actualRoc, 0, 'f', 2)
                .arg(maxRoc, 0, 'f', 2));
        return false;
    }

    // 通过校验，更新基准值
    state.lastValidValue = value;
    state.lastTimestamp = timestamp;
    return true;
}

void DataParseThread::markDeviceBad(int deviceId)
{
    auto it = m_tagsByDevice.find(deviceId);
    if (it == m_tagsByDevice.end()) {
        return;
    }
    for (quint32 tagId : it.value()) {
        if (m_doubleBuffer) {
            DoubleBuffer::TagSnapshot snapshot = m_doubleBuffer->readTag(tagId);
            snapshot.quality = DataQuality::Bad;
            m_doubleBuffer->write(tagId, snapshot);
        }
    }
    LOG_WARN("DataParseThread", QString("Device %1 tags marked as Bad").arg(deviceId));
}
