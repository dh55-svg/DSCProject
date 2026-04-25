#pragma once
#include "export.h"
#include "TagDef.h"
#include <QObject>
#include <QTimer>
#include <QMap>
#include <QList>
#include <QMutex>
#include <QDateTime>
#include <QSoundEffect>
struct AlarmEvent {
    QString alarmId;             // unique alarm ID
    quint32 tagId = 0;           // associated tag ID
    QString tagName;             // associated tag name
    AlarmState severity = AlarmState::Normal; // alarm severity level
    QString description;         // alarm description text
    float triggerValue = 0.0f;   // actual value at trigger time
    float thresholdValue = 0.0f; // alarm threshold limit
    qint64 triggerTime = 0;      // trigger timestamp (ms since epoch)
    qint64 acknowledgeTime = 0;  // acknowledge timestamp
    qint64 clearTime = 0;        // clear/restore timestamp
    bool acknowledged = false;   // whether operator has acknowledged
    bool active = true;          // whether still active
};
/**
 * @brief Alarm engine - core safety component
 *
 * Design principles:
 * 1. Zero miss: any value exceeding limits MUST trigger alarm
 * 2. Alarm storm control: debounce timer + hysteresis to prevent cascading
 * 3. Distinct audio: different severity levels use different alert sounds
 * 4. Audit trail: all alarms persisted to database for compliance
 */
class BUSINESS_EXPORT AlarmEngine : public QObject{
    Q_OBJECT
public:
    static AlarmEngine& instance();

    void initialize();

    // called by DataEngine when limit exceeded
    void triggerAlarm(quint32 tagId, AlarmState severity,
        float triggerValue, float thresholdValue);

    // acknowledge a specific alarm (operator pressed ACK)
    void acknowledgeAlarm(const QString& alarmId);
    void acknowledgeAlarmByTagId(quint32 tagId);
    // acknowledge all currently active alarms
    void acknowledgeAll();

    // called by DataEngine when value returns to normal
    void clearAlarm(quint32 tagId);

    // query interfaces
    QList<AlarmEvent> activeAlarms() const;
    QList<AlarmEvent> allAlarms(int limit = 1000) const;
    int activeAlarmCount() const;
    int activeAlarmCount(AlarmState severity) const;

    // alarm audio
    void setSoundEnabled(bool enabled);
signals:
    void alarmTriggered(const AlarmEvent& event);
    void alarmAcknowledged(const QString& alarmId);
    void alarmCleared(const QString& alarmId);
    void alarmCountChanged(int activeCount);
private:
    AlarmEngine() = default;
    ~AlarmEngine() override = default;
    AlarmEngine(const AlarmEngine&) = delete;
    AlarmEngine& operator=(const AlarmEngine&) = delete;

    QString generateAlarmId();

    QMap<quint32, AlarmEvent> m_activeAlarms;   // active alarms indexed by tagId
    QList<AlarmEvent> m_alarmHistory;            // alarm history, capped at 1000 entries
    QSoundEffect* m_alarmSoundHigh = nullptr;    // high priority alarm sound
    QSoundEffect* m_alarmSoundLow = nullptr;     // low priority alarm sound
    mutable QMutex m_mutex;                      // mutex (mutable for const accessor use)
    bool m_soundEnabled = true;                  // master sound toggle
    int m_alarmCounter = 0;                      // alarm ID sequence counter
};


