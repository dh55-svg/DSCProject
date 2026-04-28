#pragma once
#include <QThread>
#include <QAtomicInt>
#include <QHash>
#include <QVector>
#include "infrastructure/messaging/DoubleBuffer.h"
#include "infrastructure/messaging/LockFreeRingBuffer.h"
#include "domain/tag/TagInfo.h"
#include "domain/tag/DeadbandFilter.h"
#include "domain/tag/DeviationChecker.h"
#include "domain/tag/RateOfChangeChecker.h"

class TagManager;
class AlarmEngine;

class DataParseThread : public QThread {
    Q_OBJECT
public:
    explicit DataParseThread(QObject* parent = nullptr);
    ~DataParseThread() override;

    void setRingBuffer(LockFreeRingBuffer<RawModbusData, 8192>* queue);
    void setDoubleBuffer(DoubleBuffer* buffer);
    void setTagConfig(const QVector<TagInfo>& tags);
    void setTagManager(TagManager* mgr) { m_tagManager = mgr; }
    void setAlarmEngine(AlarmEngine* engine) { m_alarmEngine = engine; }
    void setProcessInterval(int ms) { m_processIntervalMs = ms; }
    void setSwapInterval(int ms) { m_swapIntervalMs = ms; }

    void stop();
    DoubleBuffer* doubleBuffer() const { return m_doubleBuffer; }

signals:
    void dataUpdated();
    void alarmTriggered(quint32 tagId, AlarmLimit limit, float value, float threshold);

protected:
    void run() override;

private:
    void processBatch(const std::vector<RawModbusData>& batch);
    float registerToValue(quint16 raw, float engLow, float engHigh);
    bool validateRateOfChange(quint32 tagId, float newValue, const TagInfo& cfg);
    void checkAlarmLimits(quint32 tagId, float value, const TagInfo& tag);

    LockFreeRingBuffer<RawModbusData, 8192>* m_ringBuffer = nullptr;
    DoubleBuffer* m_doubleBuffer = nullptr;
    TagManager* m_tagManager = nullptr;
    AlarmEngine* m_alarmEngine = nullptr;

    QHash<quint32, quint32> m_tagIdLookup; // (serverAddr<<16)|regAddr -> tagId
    QVector<TagInfo> m_tags;

    RateOfChangeChecker m_rocChecker;
    QHash<quint32, float> m_prevValues;

    QAtomicInt m_running;
    int m_processIntervalMs = 20;
    int m_swapIntervalMs = 50;
    qint64 m_lastSwapTime = 0;
};
