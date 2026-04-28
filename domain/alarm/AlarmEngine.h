#pragma once
#include <QObject>
#include <QTimer>
#include <QHash>
#include <QList>
#include <QMutex>
#include <QDateTime>
#include <QElapsedTimer>
#include <QSoundEffect>
#include <memory>

#include "domain/alarm/AlarmEvent.h"
#include "domain/alarm/AlarmStateMachine.h"
#include "domain/alarm/ShelveManager.h"
#include "domain/alarm/SuppressionEngine.h"
#include "domain/alarm/FloodDetector.h"
#include "domain/alarm/ChatteringGuard.h"
#include "domain/alarm/AlarmKpiMonitor.h"
#include "domain/alarm/AlarmChangeLog.h"
#include "infrastructure/persistence/IAlarmRepo.h"
#include "infrastructure/logging/ILogger.h"

class TagManager;
class DoubleBuffer;

class AlarmEngine : public QObject {
    Q_OBJECT
public:
    AlarmEngine(IAlarmRepo& alarmRepo, TagManager* tagManager, ILogger* logger = nullptr);
    ~AlarmEngine();

    void initialize();
    void setDoubleBuffer(DoubleBuffer* buffer) { m_doubleBuffer = buffer; }

    void triggerAlarm(quint32 tagId, AlarmLimit limit, float triggerValue, float thresholdValue,
                      AlarmPriority priority = AlarmPriority::Major,
                      AlarmClassification classification = AlarmClassification::Process,
                      int onDelayMs = 3000);
    void clearAlarm(quint32 tagId, float returnValue);

    bool acknowledgeAlarm(const QString& alarmId, const QString& operatorName = QString());
    bool acknowledgeAlarmByTagId(quint32 tagId, const QString& operatorName = QString());
    void acknowledgeAll(const QString& operatorName = QString());

    void acknowledgeReturnToNormal(const QString& alarmId);
    void acknowledgeReturnToNormalByTagId(quint32 tagId);
    void acknowledgeAllReturnToNormal();

    void shelveAlarm(quint32 tagId, const QString& reason, int durationSec = 3600, const QString& user = QString());
    void shelveAlarm(quint32 tagId, int durationMin);
    void unshelveAlarm(quint32 tagId);
    QList<AlarmEvent> shelvedAlarms() const;

    void suppressByDesign(quint32 tagId, const QString& reason, const QString& user, const QString& approver);
    void suppressAlarm(quint32 tagId, const QString& reason);
    void unsuppressByDesign(quint32 tagId);
    void unsuppressAlarm(quint32 tagId);

    bool addSuppressionRule(const SuppressionRule& rule) { return m_suppressionEngine.addRule(rule); }
    void removeSuppressionRule(quint32 ruleId) { m_suppressionEngine.removeRule(ruleId); }
    void setSuppressionRuleEnabled(quint32 ruleId, bool enabled) { m_suppressionEngine.setEnabled(ruleId, enabled); }
    QVector<SuppressionRule> suppressionRules() const { return m_suppressionEngine.rules(); }
    bool evaluateSuppression(quint32 tagId) const { return m_suppressionEngine.evaluate(tagId); }

    void setOutOfService(quint32 tagId, const QString& reason, const QString& user, const QString& workOrderNo);
    void setOutOfService(quint32 tagId, const QString& reason);
    void returnToService(quint32 tagId);

    void annotateAlarm(const QString& alarmId, const QString& annotation, const QString& user);
    void annotateAlarm(const QString& alarmId, const QString& annotation);

    bool setAlarmLimit(quint32 tagId, const QString& fieldName, float newValue,
                       const QString& operatorName, const QString& reason);
    bool setAlarmPriority(quint32 tagId, AlarmPriority newPriority,
                          const QString& operatorName, const QString& reason);

    QList<AlarmEvent> activeAlarms() const;
    QList<AlarmEvent> unacknowledgedAlarms() const;
    AlarmEvent alarmByTagId(quint32 tagId) const;
    QList<AlarmEvent> alarmHistory(int limit = 1000) const;
    QList<AlarmEvent> filteredAlarms(const AlarmFilter& filter) const;

    int activeAlarmCount() const;
    int activeAlarmCount(AlarmLimit limit) const;
    int activeAlarmCount(AlarmPriority priority) const;
    int unacknowledgedCount() const;
    int suppressedCount() const;
    int outOfServiceCount() const;
    QStringList areas() const;
    QList<AlarmEvent> alarmsByArea(const QString& area) const;

    AlarmKpiSnapshot kpiSnapshot() const;
    QVector<QPair<quint32, int>> topFrequentAlarms(int topN = 5) const;
    QVector<AlarmFloodEvent> floodEvents() const;
    AlarmKpiMonitor* kpiMonitor() { return &m_kpiMonitor; }
    AlarmChangeLog* changeLog() { return &m_changeLog; }

    void setSoundEnabled(bool enabled);
    bool soundEnabled() const { return m_soundEnabled; }

signals:
    void alarmTriggered(const AlarmEvent& event);
    void alarmAcknowledged(const QString& alarmId);
    void alarmReturnToNormalAcknowledged(const QString& alarmId);
    void alarmCleared(const QString& alarmId);
    void alarmShelved(quint32 tagId, const QString& reason, int durationSec);
    void alarmUnshelved(quint32 tagId);
    void alarmSuppressed(quint32 tagId, const QString& reason);
    void alarmUnsuppressed(quint32 tagId);
    void alarmOutOfService(quint32 tagId, const QString& reason);
    void alarmReturnedToService(quint32 tagId);
    void alarmEscalated(quint32 tagId, AlarmLimit oldLimit, AlarmLimit newLimit);
    void alarmParameterChanged(quint32 tagId, const QString& fieldName,
                               const QString& oldValue, const QString& newValue);
    void alarmAnnotated(const QString& alarmId, const QString& annotation);
    void chatteringAlarmDetected(quint32 tagId, int repeatCount);
    void alarmCountChanged(int activeCount, int unackCount);
    void alarmFloodDetected(const AlarmFloodEvent& floodEvent);
    void changeRecorded(const AlarmChangeRecord& record);

private:
    void onOnDelayTimeout(quint32 tagId, AlarmLimit limit, float value, float threshold,
                          AlarmPriority priority, AlarmClassification classification);
    void onShelveTimerTick();
    QString generateAlarmId();
    QString limitToString(AlarmLimit limit) const;
    QString soundPathForPriority(AlarmPriority priority) const;
    void playAlarmSound(AlarmPriority priority);
    void checkFloodCondition();

    IAlarmRepo& m_alarmRepo;
    TagManager* m_tagManager;
    ILogger* m_logger;

    QHash<quint32, AlarmEvent> m_activeAlarms;
    QList<AlarmEvent> m_alarmHistory;

    struct OnDelayEntry {
        AlarmLimit limit;
        float value;
        float threshold;
        AlarmPriority priority;
        AlarmClassification classification;
        int onDelayMs = 3000;
        QElapsedTimer elapsed;
    };
    QHash<quint32, OnDelayEntry> m_onDelayEntries;
    QTimer* m_onDelayTimer = nullptr;

    ShelveManager m_shelveManager;
    QTimer* m_shelveCheckTimer = nullptr;
    SuppressionEngine m_suppressionEngine;
    FloodDetector m_floodDetector;
    ChatteringGuard m_chatteringGuard;

    QSoundEffect* m_soundCritical = nullptr;
    QSoundEffect* m_soundMajor = nullptr;
    QSoundEffect* m_soundMinor = nullptr;
    bool m_soundEnabled = true;

    AlarmKpiMonitor m_kpiMonitor;
    AlarmChangeLog m_changeLog;

    DoubleBuffer* m_doubleBuffer = nullptr;

    int m_alarmCounter = 0;
    mutable QMutex m_mutex;
};
