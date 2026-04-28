#include "DataParseThread.h"
#include "domain/tag/TagManager.h"
#include "domain/alarm/AlarmEngine.h"
#include <QElapsedTimer>
#include <QDateTime>
#include <cmath>

DataParseThread::DataParseThread(QObject* parent) : QThread(parent) {
    m_running.storeRelaxed(0);
}

DataParseThread::~DataParseThread() { stop(); }

void DataParseThread::setRingBuffer(LockFreeRingBuffer<RawModbusData, 8192>* queue) { m_ringBuffer = queue; }
void DataParseThread::setDoubleBuffer(DoubleBuffer* buffer) { m_doubleBuffer = buffer; }

void DataParseThread::setTagConfig(const QVector<TagInfo>& tags) {
    m_tags = tags;
    m_tagIdLookup.clear();
    for (const auto& tag : tags) {
        quint32 key = (static_cast<quint32>(tag.modbusServerAddr) << 16) | static_cast<quint32>(tag.modbusRegAddr);
        m_tagIdLookup[key] = tag.tagId;
    }
}

void DataParseThread::stop() {
    m_running.storeRelaxed(0);
    if (isRunning()) {
        quit();
        wait(3000);
    }
}

void DataParseThread::run() {
    m_running.storeRelaxed(1);
    m_lastSwapTime = QDateTime::currentMSecsSinceEpoch();

    while (m_running.loadRelaxed()) {
        std::vector<RawModbusData> batch;
        size_t count = m_ringBuffer->dequeueBatch(batch, 256);

        if (count > 0) {
            processBatch(batch);
        }

        qint64 now = QDateTime::currentMSecsSinceEpoch();
        if (now - m_lastSwapTime >= m_swapIntervalMs) {
            m_doubleBuffer->commit();
            m_lastSwapTime = now;
            emit dataUpdated();
        }

        QThread::msleep(m_processIntervalMs);
    }
}

void DataParseThread::processBatch(const std::vector<RawModbusData>& batch) {
    for (const auto& raw : batch) {
        for (int i = 0; i < raw.count; ++i) {
            quint32 key = (static_cast<quint32>(raw.serverAddr) << 16) | static_cast<quint32>(raw.startAddr + i);
            auto idIt = m_tagIdLookup.find(key);
            if (idIt == m_tagIdLookup.end()) continue;

            quint32 tagId = idIt.value();
            TagInfo cfg;
            if (m_tagManager) cfg = m_tagManager->getTag(tagId);
            else cfg = TagInfo{};

            float engValue = registerToValue(raw.values[i], cfg.engLow, cfg.engHigh);

            // ROC validation
            if (validateRateOfChange(tagId, engValue, cfg)) continue;

            // Write to DoubleBuffer
            DoubleBuffer::TagSnapshot snap;
            snap.tagId = tagId;
            snap.currentValue = engValue;
            snap.timestamp = QDateTime::currentMSecsSinceEpoch();
            snap.quality = DataQuality::Good;
            m_doubleBuffer->write(tagId, snap);

            m_prevValues[tagId] = engValue;

            // Alarm checks
            checkAlarmLimits(tagId, engValue, cfg);

            // Deviation check
            if (cfg.deviationEnabled && m_tagManager) {
                float sp = m_prevValues.value(tagId ^ 0x80000000, 0); // rough SP lookup
                if (DeviationChecker::exceedsDeviation(engValue, sp, cfg.deviationLimit)) {
                    if (m_alarmEngine)
                        m_alarmEngine->triggerAlarm(tagId, AlarmLimit::Deviation, engValue, sp + cfg.deviationLimit,
                            AlarmPriority::Major, AlarmClassification::Process);
                }
            }

            // ROC alarm
            if (cfg.rateOfChangeEnabled && m_rocChecker.exceedsLimit(tagId, engValue, cfg)) {
                if (m_alarmEngine)
                    m_alarmEngine->triggerAlarm(tagId, AlarmLimit::RateOfChange, engValue, cfg.rateOfChangeLimit,
                        AlarmPriority::Major, AlarmClassification::Process);
            }
        }
    }
}

float DataParseThread::registerToValue(quint16 raw, float engLow, float engHigh) {
    float range = engHigh - engLow;
    if (range <= 0) return engLow;
    return engLow + (static_cast<float>(raw) / 65535.0f) * range;
}

bool DataParseThread::validateRateOfChange(quint32 tagId, float newValue, const TagInfo& cfg) {
    if (cfg.rateOfChangeLimit <= 0.0f) return false;
    auto it = m_prevValues.find(tagId);
    if (it == m_prevValues.end()) return false;

    float dt = static_cast<float>(m_processIntervalMs) / 1000.0f;
    if (dt <= 0) return false;
    float rate = std::abs(newValue - it.value()) / dt;
    return rate > cfg.rateOfChangeLimit * 3.0f; // spike detection: 3x normal ROC limit
}

void DataParseThread::checkAlarmLimits(quint32 tagId, float value, const TagInfo& tag) {
    if (!tag.alarmEnabled || !m_alarmEngine) return;
    float prevValue = m_prevValues.value(tagId, value);

    if (tag.highHighEnabled && DeadbandFilter::exceedsDeadband(value, tag.highHighLimit, tag.deadband, AlarmLimit::HighHigh, prevValue)) {
        m_alarmEngine->triggerAlarm(tagId, AlarmLimit::HighHigh, value, tag.highHighLimit, AlarmPriority::Critical);
    } else if (tag.highEnabled && DeadbandFilter::exceedsDeadband(value, tag.highLimit, tag.deadband, AlarmLimit::High, prevValue)) {
        m_alarmEngine->triggerAlarm(tagId, AlarmLimit::High, value, tag.highLimit, AlarmPriority::Major);
    } else if (tag.lowEnabled && DeadbandFilter::exceedsDeadband(value, tag.lowLimit, tag.deadband, AlarmLimit::Low, prevValue)) {
        m_alarmEngine->triggerAlarm(tagId, AlarmLimit::Low, value, tag.lowLimit, AlarmPriority::Major);
    } else if (tag.lowLowEnabled && DeadbandFilter::exceedsDeadband(value, tag.lowLowLimit, tag.deadband, AlarmLimit::LowLow, prevValue)) {
        m_alarmEngine->triggerAlarm(tagId, AlarmLimit::LowLow, value, tag.lowLowLimit, AlarmPriority::Critical);
    } else {
        // Check if value returned to normal
        bool hhOK = !tag.highHighEnabled || DeadbandFilter::returnsToNormal(value, tag.highHighLimit, tag.deadband, AlarmLimit::HighHigh);
        bool hOK = !tag.highEnabled || DeadbandFilter::returnsToNormal(value, tag.highLimit, tag.deadband, AlarmLimit::High);
        bool lOK = !tag.lowEnabled || DeadbandFilter::returnsToNormal(value, tag.lowLimit, tag.deadband, AlarmLimit::Low);
        bool llOK = !tag.lowLowEnabled || DeadbandFilter::returnsToNormal(value, tag.lowLowLimit, tag.deadband, AlarmLimit::LowLow);

        if (hhOK && hOK && lOK && llOK) {
            m_alarmEngine->clearAlarm(tagId, value);
        }
    }
}
