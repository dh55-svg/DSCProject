#pragma once
#include <QObject>
#include "domain/alarm/AlarmEngine.h"

class AlarmController : public QObject {
    Q_OBJECT
public:
    explicit AlarmController(AlarmEngine& engine, ILogger* logger = nullptr);

    AlarmEngine& engine() { return m_engine; }

    void acknowledgeAlarm(const QString& alarmId, const QString& user = QString());
    void acknowledgeAll(const QString& user = QString());
    void shelveAlarm(quint32 tagId, const QString& reason, int durationSec, const QString& user);
    void unshelveAlarm(quint32 tagId);
    void suppressAlarm(quint32 tagId, const QString& reason, const QString& user);
    void unsuppressAlarm(quint32 tagId);
    void setOutOfService(quint32 tagId, const QString& reason, const QString& user);
    void returnToService(quint32 tagId);
    void annotateAlarm(const QString& alarmId, const QString& text, const QString& user);
    void setSoundEnabled(bool enabled);

signals:
    void alarmTriggered(const AlarmEvent& event);
    void alarmCountChanged(int activeCount, int unackCount);

private:
    AlarmEngine& m_engine;
    ILogger* m_logger;
};
