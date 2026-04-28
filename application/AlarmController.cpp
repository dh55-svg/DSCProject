#include "AlarmController.h"

AlarmController::AlarmController(AlarmEngine& engine, ILogger* logger)
    : m_engine(engine), m_logger(logger)
{
    connect(&m_engine, &AlarmEngine::alarmTriggered, this, &AlarmController::alarmTriggered);
    connect(&m_engine, &AlarmEngine::alarmCountChanged, this, &AlarmController::alarmCountChanged);
}

void AlarmController::acknowledgeAlarm(const QString& alarmId, const QString& user) {
    m_engine.acknowledgeAlarm(alarmId, user);
}

void AlarmController::acknowledgeAll(const QString& user) {
    m_engine.acknowledgeAll(user);
}

void AlarmController::shelveAlarm(quint32 tagId, const QString& reason, int durationSec, const QString& user) {
    m_engine.shelveAlarm(tagId, reason, durationSec, user);
}

void AlarmController::unshelveAlarm(quint32 tagId) {
    m_engine.unshelveAlarm(tagId);
}

void AlarmController::suppressAlarm(quint32 tagId, const QString& reason, const QString& user) {
    m_engine.suppressByDesign(tagId, reason, user, QString());
}

void AlarmController::unsuppressAlarm(quint32 tagId) {
    m_engine.unsuppressByDesign(tagId);
}

void AlarmController::setOutOfService(quint32 tagId, const QString& reason, const QString& user) {
    m_engine.setOutOfService(tagId, reason, user, QString());
}

void AlarmController::returnToService(quint32 tagId) {
    m_engine.returnToService(tagId);
}

void AlarmController::annotateAlarm(const QString& alarmId, const QString& text, const QString& user) {
    m_engine.annotateAlarm(alarmId, text, user);
}

void AlarmController::setSoundEnabled(bool enabled) {
    m_engine.setSoundEnabled(enabled);
}
