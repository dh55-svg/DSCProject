#include "AlarmEngine.h"
#include "domain/tag/TagManager.h"
#include "infrastructure/persistence/IAlarmRepo.h"
#include <QFile>

AlarmEngine::AlarmEngine(IAlarmRepo& alarmRepo, TagManager* tagManager, ILogger* logger)
    : m_alarmRepo(alarmRepo), m_tagManager(tagManager), m_logger(logger)
{
    m_onDelayTimer = new QTimer(this);
    m_onDelayTimer->setInterval(500);
    connect(m_onDelayTimer, &QTimer::timeout, this, [this]() {
        QMutexLocker lock(&m_mutex);
        QList<quint32> toTrigger;
        for (auto it = m_onDelayEntries.begin(); it != m_onDelayEntries.end(); ++it) {
            if (it->elapsed.hasExpired(it->onDelayMs)) toTrigger.append(it.key());
        }
        lock.unlock();
        for (quint32 tagId : toTrigger) {
            OnDelayEntry entry;
            { QMutexLocker l(&m_mutex);
              auto it = m_onDelayEntries.find(tagId);
              if (it == m_onDelayEntries.end()) continue;
              entry = it.value();
              m_onDelayEntries.erase(it); }
            onOnDelayTimeout(tagId, entry.limit, entry.value, entry.threshold,
                             entry.priority, entry.classification);
        }
    });

    m_shelveCheckTimer = new QTimer(this);
    m_shelveCheckTimer->setInterval(10000);
    connect(m_shelveCheckTimer, &QTimer::timeout, this, &AlarmEngine::onShelveTimerTick);
}

AlarmEngine::~AlarmEngine() {
    m_onDelayTimer->stop();
    m_shelveCheckTimer->stop();
}

void AlarmEngine::initialize() {
    m_soundCritical = new QSoundEffect(this);
    m_soundMajor = new QSoundEffect(this);
    m_soundMinor = new QSoundEffect(this);

    auto loadSound = [](QSoundEffect* effect, const QString& path, float vol) {
        if (QFile::exists(path)) { effect->setSource(QUrl::fromLocalFile(path)); effect->setVolume(vol); return true; }
        return false;
    };
    loadSound(m_soundCritical, "./sounds/alarm_critical.wav", 1.0f);
    loadSound(m_soundMajor, "./sounds/alarm_high.wav", 0.8f);
    loadSound(m_soundMinor, "./sounds/alarm_low.wav", 0.5f);

    m_onDelayTimer->start();
    m_shelveCheckTimer->start();
    if (m_logger) m_logger->info("ISA-18.2 报警引擎初始化完成");
}

void AlarmEngine::triggerAlarm(quint32 tagId, AlarmLimit limit, float triggerValue, float thresholdValue,
                                AlarmPriority priority, AlarmClassification classification, int onDelayMs) {
    QMutexLocker lock(&m_mutex);

    auto activeIt = m_activeAlarms.find(tagId);
    if (activeIt != m_activeAlarms.end() && activeIt->shelved) return;

    // Escalation: existing active alarm with higher limit
    if (activeIt != m_activeAlarms.end() && activeIt->isActive()) {
        if (limit > activeIt->limit) {
            AlarmLimit oldLimit = activeIt->limit;
            activeIt->limit = limit;
            activeIt->priority = priority;
            activeIt->classification = classification;
            activeIt->triggerValue = triggerValue;
            activeIt->thresholdValue = thresholdValue;
            activeIt->triggerTime = QDateTime::currentMSecsSinceEpoch();
            activeIt->description = QString("%1报警升级，当前值=%2，限值=%3")
                .arg(limitToString(limit)).arg(triggerValue, 0, 'f', 1).arg(thresholdValue, 0, 'f', 1);
            activeIt->state = AlarmState::ActiveUnacknowledged;
            activeIt->acknowledged = false;
            activeIt->acknowledgeTime = 0;
            lock.unlock();
            playAlarmSound(priority);
            emit alarmTriggered(*activeIt);
            emit alarmEscalated(tagId, oldLimit, limit);
            emit alarmCountChanged(activeAlarmCount(), unacknowledgedCount());
            return;
        }
        return;
    }

    // RTN re-trigger
    if (activeIt != m_activeAlarms.end() &&
        (activeIt->state == AlarmState::ReturnToNormalUnacknowledged ||
         activeIt->state == AlarmState::ReturnToNormalAcknowledged)) {
        activeIt->state = AlarmState::ActiveUnacknowledged;
        activeIt->limit = limit;
        activeIt->priority = priority;
        activeIt->classification = classification;
        activeIt->triggerValue = triggerValue;
        activeIt->thresholdValue = thresholdValue;
        activeIt->triggerTime = QDateTime::currentMSecsSinceEpoch();
        activeIt->acknowledged = false;
        activeIt->acknowledgeTime = 0;
        activeIt->returnToNormalTime = 0;
        activeIt->returnAckTime = 0;
        activeIt->description = QString("%1报警，当前值=%2，限值=%3")
            .arg(limitToString(limit)).arg(triggerValue, 0, 'f', 1).arg(thresholdValue, 0, 'f', 1);
        lock.unlock();
        playAlarmSound(priority);
        emit alarmTriggered(*activeIt);
        emit alarmCountChanged(activeAlarmCount(), unacknowledgedCount());
        return;
    }

    // New alarm: enter On-Delay
    auto delayIt = m_onDelayEntries.find(tagId);
    if (delayIt != m_onDelayEntries.end()) {
        if (limit > delayIt->limit) {
            delayIt->limit = limit;
            delayIt->value = triggerValue;
            delayIt->threshold = thresholdValue;
            delayIt->priority = priority;
            delayIt->classification = classification;
        }
        return;
    }

    OnDelayEntry entry;
    entry.limit = limit;
    entry.value = triggerValue;
    entry.threshold = thresholdValue;
    entry.priority = priority;
    entry.classification = classification;
    entry.elapsed.start();
    m_onDelayEntries.insert(tagId, entry);
}

void AlarmEngine::onOnDelayTimeout(quint32 tagId, AlarmLimit limit, float value, float threshold,
                                    AlarmPriority priority, AlarmClassification classification) {
    QMutexLocker lock(&m_mutex);

    auto activeIt = m_activeAlarms.find(tagId);
    if (activeIt != m_activeAlarms.end() && activeIt->isActive()) return;

    // Chattering check
    if (m_chatteringGuard.check(tagId, 3)) {
        if (m_logger) m_logger->warn(QString("震荡报警检测: tagId=%1").arg(tagId));
        emit chatteringAlarmDetected(tagId, 3);
        return;
    }

    QString tagName;
    if (m_tagManager) {
        TagInfo ti = m_tagManager->getTag(tagId);
        tagName = ti.tagName;
    }

    AlarmEvent event;
    event.alarmId = generateAlarmId();
    event.tagId = tagId;
    event.tagName = tagName;
    event.limit = limit;
    event.priority = priority;
    event.classification = classification;
    event.triggerValue = value;
    event.thresholdValue = threshold;
    event.triggerTime = QDateTime::currentMSecsSinceEpoch();
    event.state = AlarmState::ActiveUnacknowledged;
    event.description = QString("%1报警，当前值=%2，限值=%3")
        .arg(limitToString(limit)).arg(value, 0, 'f', 1).arg(threshold, 0, 'f', 1);

    m_activeAlarms.insert(tagId, event);
    m_alarmHistory.prepend(event);
    if (m_alarmHistory.size() > 2000) m_alarmHistory.removeLast();

    m_alarmRepo.insertEvent(event);
    m_kpiMonitor.recordAlarm(tagName);
    m_floodDetector.recordAlarm(tagId, tagName);

    lock.unlock();
    playAlarmSound(priority);
    emit alarmTriggered(event);
    emit alarmCountChanged(activeAlarmCount(), unacknowledgedCount());

    if (m_floodDetector.isInFlood()) {
        emit alarmFloodDetected(m_floodDetector.currentFlood());
    }
}

void AlarmEngine::clearAlarm(quint32 tagId, float returnValue) {
    QMutexLocker lock(&m_mutex);
    m_onDelayEntries.remove(tagId);
    auto it = m_activeAlarms.find(tagId);
    if (it == m_activeAlarms.end() || !it->isActive()) return;

    it->state = AlarmState::ReturnToNormalUnacknowledged;
    it->returnToNormalTime = QDateTime::currentMSecsSinceEpoch();
    it->returnValue = returnValue;
    it->active = false;
    lock.unlock();
    emit alarmCountChanged(activeAlarmCount(), unacknowledgedCount());
}

bool AlarmEngine::acknowledgeAlarm(const QString& alarmId, const QString& operatorName) {
    QMutexLocker lock(&m_mutex);
    for (auto it = m_activeAlarms.begin(); it != m_activeAlarms.end(); ++it) {
        if (it->alarmId == alarmId && it->state == AlarmState::ActiveUnacknowledged) {
            it->state = AlarmState::ActiveAcknowledged;
            it->acknowledgeTime = QDateTime::currentMSecsSinceEpoch();
            it->acknowledged = true;
            if (!operatorName.isEmpty()) it->acknowledgeUser = operatorName;
            QString id = alarmId;
            lock.unlock();
            emit alarmAcknowledged(id);
            emit alarmCountChanged(activeAlarmCount(), unacknowledgedCount());
            m_alarmRepo.updateAck(id, operatorName.isEmpty() ? "operator" : operatorName, QDateTime::currentMSecsSinceEpoch());
            return true;
        }
    }
    return false;
}

bool AlarmEngine::acknowledgeAlarmByTagId(quint32 tagId, const QString& operatorName) {
    QMutexLocker lock(&m_mutex);
    auto it = m_activeAlarms.find(tagId);
    if (it == m_activeAlarms.end() || it->state != AlarmState::ActiveUnacknowledged) return false;
    it->state = AlarmState::ActiveAcknowledged;
    it->acknowledgeTime = QDateTime::currentMSecsSinceEpoch();
    it->acknowledged = true;
    if (!operatorName.isEmpty()) it->acknowledgeUser = operatorName;
    QString alarmId = it->alarmId;
    lock.unlock();
    emit alarmAcknowledged(alarmId);
    emit alarmCountChanged(activeAlarmCount(), unacknowledgedCount());
    return true;
}

void AlarmEngine::acknowledgeAll(const QString& operatorName) {
    QMutexLocker lock(&m_mutex);
    QList<QString> acked;
    for (auto it = m_activeAlarms.begin(); it != m_activeAlarms.end(); ++it) {
        if (it->state == AlarmState::ActiveUnacknowledged) {
            it->state = AlarmState::ActiveAcknowledged;
            it->acknowledgeTime = QDateTime::currentMSecsSinceEpoch();
            it->acknowledged = true;
            if (!operatorName.isEmpty()) it->acknowledgeUser = operatorName;
            acked.append(it->alarmId);
        }
    }
    lock.unlock();
    for (const auto& id : acked) emit alarmAcknowledged(id);
    emit alarmCountChanged(activeAlarmCount(), unacknowledgedCount());
}

void AlarmEngine::acknowledgeReturnToNormal(const QString& alarmId) {
    QMutexLocker lock(&m_mutex);
    for (auto it = m_activeAlarms.begin(); it != m_activeAlarms.end(); ++it) {
        if (it->alarmId == alarmId && it->state == AlarmState::ReturnToNormalUnacknowledged) {
            it->state = AlarmState::ReturnToNormalAcknowledged;
            it->returnAckTime = QDateTime::currentMSecsSinceEpoch();
            QString id = alarmId;
            m_activeAlarms.erase(it);
            lock.unlock();
            emit alarmReturnToNormalAcknowledged(id);
            emit alarmCleared(id);
            emit alarmCountChanged(activeAlarmCount(), unacknowledgedCount());
            return;
        }
    }
}

void AlarmEngine::acknowledgeReturnToNormalByTagId(quint32 tagId) {
    QMutexLocker lock(&m_mutex);
    auto it = m_activeAlarms.find(tagId);
    if (it == m_activeAlarms.end() || it->state != AlarmState::ReturnToNormalUnacknowledged) return;
    QString alarmId = it->alarmId;
    m_activeAlarms.erase(it);
    lock.unlock();
    emit alarmReturnToNormalAcknowledged(alarmId);
    emit alarmCleared(alarmId);
    emit alarmCountChanged(activeAlarmCount(), unacknowledgedCount());
}

void AlarmEngine::acknowledgeAllReturnToNormal() {
    QMutexLocker lock(&m_mutex);
    QList<QString> cleared;
    auto it = m_activeAlarms.begin();
    while (it != m_activeAlarms.end()) {
        if (it->state == AlarmState::ReturnToNormalUnacknowledged) {
            cleared.append(it->alarmId);
            it = m_activeAlarms.erase(it);
        } else { ++it; }
    }
    lock.unlock();
    for (const auto& id : cleared) { emit alarmReturnToNormalAcknowledged(id); emit alarmCleared(id); }
    emit alarmCountChanged(activeAlarmCount(), unacknowledgedCount());
}

void AlarmEngine::shelveAlarm(quint32 tagId, const QString& reason, int durationSec, const QString& user) {
    QMutexLocker lock(&m_mutex);
    auto it = m_activeAlarms.find(tagId);
    if (it == m_activeAlarms.end()) return;

    it->shelved = true;
    it->shelvedTime = QDateTime::currentMSecsSinceEpoch();
    it->shelveReason = reason;
    it->shelveDurationSec = durationSec;
    if (!user.isEmpty()) it->shelveUser = user;
    it->state = AlarmState::Shelved;

    m_shelveManager.shelve(tagId, durationSec);
    lock.unlock();
    emit alarmShelved(tagId, reason, durationSec);
    emit alarmCountChanged(activeAlarmCount(), unacknowledgedCount());
}

void AlarmEngine::shelveAlarm(quint32 tagId, int durationMin) {
    shelveAlarm(tagId, QString("操作员屏蔽"), durationMin * 60);
}

void AlarmEngine::unshelveAlarm(quint32 tagId) {
    QMutexLocker lock(&m_mutex);
    m_shelveManager.unshelve(tagId);
    auto it = m_activeAlarms.find(tagId);
    if (it == m_activeAlarms.end() || !it->shelved) return;

    it->shelved = false;
    it->shelvedTime = 0;
    it->shelveReason.clear();
    it->state = it->isActive() ? AlarmState::ActiveUnacknowledged : AlarmState::ReturnToNormalUnacknowledged;
    lock.unlock();
    emit alarmUnshelved(tagId);
    emit alarmCountChanged(activeAlarmCount(), unacknowledgedCount());
}

QList<AlarmEvent> AlarmEngine::shelvedAlarms() const {
    QMutexLocker lock(&m_mutex);
    QList<AlarmEvent> result;
    for (const auto& e : m_activeAlarms) { if (e.shelved) result.append(e); }
    return result;
}

void AlarmEngine::suppressByDesign(quint32 tagId, const QString& reason, const QString& user, const QString& approver) {
    QMutexLocker lock(&m_mutex);
    auto it = m_activeAlarms.find(tagId);
    if (it != m_activeAlarms.end()) {
        it->suppressionType = AlarmSuppressionType::DesignSuppression;
        it->suppressionReason = reason;
        it->suppressionUser = user;
        it->suppressionTime = QDateTime::currentMSecsSinceEpoch();
        it->state = AlarmState::SuppressedByDesign;
    }
    lock.unlock();
    emit alarmSuppressed(tagId, reason);
    emit alarmCountChanged(activeAlarmCount(), unacknowledgedCount());
}

void AlarmEngine::suppressAlarm(quint32 tagId, const QString& reason) {
    suppressByDesign(tagId, reason, QString(), QString());
}

void AlarmEngine::unsuppressByDesign(quint32 tagId) {
    QMutexLocker lock(&m_mutex);
    auto it = m_activeAlarms.find(tagId);
    if (it == m_activeAlarms.end() || it->state != AlarmState::SuppressedByDesign) return;
    it->suppressionType = AlarmSuppressionType::None;
    it->suppressionReason.clear();
    it->state = it->isActive() ? AlarmState::ActiveUnacknowledged : AlarmState::ReturnToNormalUnacknowledged;
    lock.unlock();
    emit alarmUnsuppressed(tagId);
    emit alarmCountChanged(activeAlarmCount(), unacknowledgedCount());
}

void AlarmEngine::unsuppressAlarm(quint32 tagId) { unsuppressByDesign(tagId); }

void AlarmEngine::setOutOfService(quint32 tagId, const QString& reason, const QString& user, const QString& workOrderNo) {
    QMutexLocker lock(&m_mutex);
    auto it = m_activeAlarms.find(tagId);
    if (it != m_activeAlarms.end()) {
        it->outOfService = true;
        it->outOfServiceReason = reason;
        it->outOfServiceUser = user;
        it->workOrderNo = workOrderNo;
        it->state = AlarmState::OutOfService;
    }
    lock.unlock();
    emit alarmOutOfService(tagId, reason);
    emit alarmCountChanged(activeAlarmCount(), unacknowledgedCount());
}

void AlarmEngine::setOutOfService(quint32 tagId, const QString& reason) {
    setOutOfService(tagId, reason, QString(), QString());
}

void AlarmEngine::returnToService(quint32 tagId) {
    QMutexLocker lock(&m_mutex);
    auto it = m_activeAlarms.find(tagId);
    if (it == m_activeAlarms.end() || !it->outOfService) return;
    it->outOfService = false;
    it->outOfServiceReason.clear();
    it->state = it->isActive() ? AlarmState::ActiveUnacknowledged : AlarmState::ReturnToNormalUnacknowledged;
    lock.unlock();
    emit alarmReturnedToService(tagId);
    emit alarmCountChanged(activeAlarmCount(), unacknowledgedCount());
}

void AlarmEngine::annotateAlarm(const QString& alarmId, const QString& annotation, const QString& user) {
    QMutexLocker lock(&m_mutex);
    for (auto it = m_activeAlarms.begin(); it != m_activeAlarms.end(); ++it) {
        if (it->alarmId == alarmId) {
            it->operatorAnnotation = annotation;
            it->annotationTime = QDateTime::currentMSecsSinceEpoch();
            it->annotationUser = user;
            lock.unlock();
            emit alarmAnnotated(alarmId, annotation);
            return;
        }
    }
}

void AlarmEngine::annotateAlarm(const QString& alarmId, const QString& annotation) {
    annotateAlarm(alarmId, annotation, QString());
}

bool AlarmEngine::setAlarmLimit(quint32 tagId, const QString& fieldName, float newValue,
                                 const QString& operatorName, const QString& reason) {
    QString oldValue;
    if (m_tagManager) {
        TagInfo tag = m_tagManager->getTag(tagId);
        if (fieldName == "highHighLimit") oldValue = QString::number(tag.highHighLimit, 'f', 1);
        else if (fieldName == "highLimit") oldValue = QString::number(tag.highLimit, 'f', 1);
        else if (fieldName == "lowLimit") oldValue = QString::number(tag.lowLimit, 'f', 1);
        else if (fieldName == "lowLowLimit") oldValue = QString::number(tag.lowLowLimit, 'f', 1);
        else if (fieldName == "deadband") oldValue = QString::number(tag.deadband, 'f', 1);
        else if (fieldName == "onDelayMs") oldValue = QString::number(tag.onDelayMs);
    }

    AlarmChangeRecord rec;
    rec.tagId = tagId;
    rec.fieldName = fieldName;
    rec.oldValue = oldValue;
    rec.newValue = QString::number(newValue, 'f', 1);
    rec.operatorName = operatorName;
    rec.reason = reason;
    m_changeLog.recordChange(rec);

    emit alarmParameterChanged(tagId, fieldName, rec.oldValue, rec.newValue);
    emit changeRecorded(rec);
    return true;
}

bool AlarmEngine::setAlarmPriority(quint32 tagId, AlarmPriority newPriority,
                                    const QString& operatorName, const QString& reason) {
    QString oldVal;
    if (m_tagManager) oldVal = QString::number(static_cast<int>(m_tagManager->getTag(tagId).priority));

    AlarmChangeRecord rec;
    rec.tagId = tagId;
    rec.fieldName = "priority";
    rec.oldValue = oldVal;
    rec.newValue = QString::number(static_cast<int>(newPriority));
    rec.operatorName = operatorName;
    rec.reason = reason;
    m_changeLog.recordChange(rec);

    emit alarmParameterChanged(tagId, "priority", rec.oldValue, rec.newValue);
    emit changeRecorded(rec);
    return true;
}

void AlarmEngine::onShelveTimerTick() {
    QMutexLocker lock(&m_mutex);
    qint64 now = QDateTime::currentMSecsSinceEpoch();

    // Check expired shelves
    QList<quint32> expired = m_shelveManager.checkExpired();

    // Update KPI stats
    int totalActive = 0, staleCount = 0, shelvedCount = 0;
    qint64 staleCutoff = now - static_cast<qint64>(m_kpiMonitor.staleThresholdMin()) * 60 * 1000;
    for (const auto& ev : m_activeAlarms) {
        if (ev.shelved) {
            shelvedCount++;
        } else {
            totalActive++;
            if (ev.state == AlarmState::ActiveUnacknowledged && ev.triggerTime > 0 && ev.triggerTime < staleCutoff)
                staleCount++;
        }
    }
    m_kpiMonitor.setExternalStats(totalActive, staleCount, shelvedCount);
    lock.unlock();

    for (quint32 tagId : expired) unshelveAlarm(tagId);
}

// Query methods
QList<AlarmEvent> AlarmEngine::activeAlarms() const {
    QMutexLocker lock(&m_mutex);
    QList<AlarmEvent> result;
    for (const auto& e : m_activeAlarms) { if (!e.shelved) result.append(e); }
    return result;
}

QList<AlarmEvent> AlarmEngine::unacknowledgedAlarms() const {
    QMutexLocker lock(&m_mutex);
    QList<AlarmEvent> result;
    for (const auto& e : m_activeAlarms) { if (e.needsAttention()) result.append(e); }
    return result;
}

AlarmEvent AlarmEngine::alarmByTagId(quint32 tagId) const {
    QMutexLocker lock(&m_mutex);
    auto it = m_activeAlarms.find(tagId);
    return (it != m_activeAlarms.end()) ? *it : AlarmEvent();
}

QList<AlarmEvent> AlarmEngine::alarmHistory(int limit) const {
    QMutexLocker lock(&m_mutex);
    return m_alarmHistory.mid(0, qMin(limit, m_alarmHistory.size()));
}

QList<AlarmEvent> AlarmEngine::filteredAlarms(const AlarmFilter& filter) const {
    QMutexLocker lock(&m_mutex);
    QList<AlarmEvent> result;
    for (const auto& e : m_activeAlarms) {
        if (!filter.priorities.isEmpty() && !filter.priorities.contains(e.priority)) continue;
        if (!filter.classifications.isEmpty() && !filter.classifications.contains(e.classification)) continue;
        if (!filter.states.isEmpty() && !filter.states.contains(e.state)) continue;
        if (!filter.areas.isEmpty() && !filter.areas.contains(e.area)) continue;
        if (filter.fromTime > 0 && e.triggerTime < filter.fromTime) continue;
        if (filter.toTime > 0 && e.triggerTime > filter.toTime) continue;
        if (!filter.keyword.isEmpty() && !e.tagName.contains(filter.keyword, Qt::CaseInsensitive)
            && !e.description.contains(filter.keyword, Qt::CaseInsensitive)) continue;
        if (!filter.includeShelved && e.shelved) continue;
        if (!filter.includeSuppressed && e.isSuppressed()) continue;
        if (!filter.includeOutOfService && e.outOfService) continue;
        result.append(e);
    }
    return result;
}

int AlarmEngine::activeAlarmCount() const {
    QMutexLocker lock(&m_mutex);
    int c = 0;
    for (const auto& e : m_activeAlarms) { if (!e.shelved) c++; }
    return c;
}

int AlarmEngine::activeAlarmCount(AlarmLimit limit) const {
    QMutexLocker lock(&m_mutex);
    int c = 0;
    for (const auto& e : m_activeAlarms) { if (!e.shelved && e.limit == limit) c++; }
    return c;
}

int AlarmEngine::activeAlarmCount(AlarmPriority priority) const {
    QMutexLocker lock(&m_mutex);
    int c = 0;
    for (const auto& e : m_activeAlarms) { if (!e.shelved && e.priority == priority) c++; }
    return c;
}

int AlarmEngine::unacknowledgedCount() const {
    QMutexLocker lock(&m_mutex);
    int c = 0;
    for (const auto& e : m_activeAlarms) { if (e.needsAttention()) c++; }
    return c;
}

int AlarmEngine::suppressedCount() const {
    QMutexLocker lock(&m_mutex);
    int c = 0;
    for (const auto& e : m_activeAlarms) { if (e.isSuppressed()) c++; }
    return c;
}

int AlarmEngine::outOfServiceCount() const {
    QMutexLocker lock(&m_mutex);
    int c = 0;
    for (const auto& e : m_activeAlarms) { if (e.outOfService) c++; }
    return c;
}

QStringList AlarmEngine::areas() const {
    QMutexLocker lock(&m_mutex);
    QStringList result;
    for (const auto& e : m_activeAlarms) {
        if (!e.area.isEmpty() && !result.contains(e.area)) result.append(e.area);
    }
    return result;
}

QList<AlarmEvent> AlarmEngine::alarmsByArea(const QString& area) const {
    QMutexLocker lock(&m_mutex);
    QList<AlarmEvent> result;
    for (const auto& e : m_activeAlarms) { if (e.area == area) result.append(e); }
    return result;
}

AlarmKpiSnapshot AlarmEngine::kpiSnapshot() const { return m_kpiMonitor.snapshot(); }

QVector<QPair<quint32, int>> AlarmEngine::topFrequentAlarms(int topN) const {
    return m_kpiMonitor.topFrequent(topN);
}

QVector<AlarmFloodEvent> AlarmEngine::floodEvents() const { return m_floodDetector.pastFloods(); }

void AlarmEngine::setSoundEnabled(bool enabled) {
    m_soundEnabled = enabled;
    if (!enabled) {
        if (m_soundCritical) m_soundCritical->stop();
        if (m_soundMajor) m_soundMajor->stop();
        if (m_soundMinor) m_soundMinor->stop();
    }
}

void AlarmEngine::playAlarmSound(AlarmPriority priority) {
    if (!m_soundEnabled) return;
    QSoundEffect* effect = nullptr;
    switch (priority) {
    case AlarmPriority::Critical: effect = m_soundCritical; break;
    case AlarmPriority::Major:    effect = m_soundMajor; break;
    case AlarmPriority::Minor:
    case AlarmPriority::Advisory: effect = m_soundMinor; break;
    }
    if (effect) effect->play();
}

QString AlarmEngine::generateAlarmId() {
    qint64 now = QDateTime::currentMSecsSinceEpoch();
    return QString("ALM_%1_%2")
        .arg(QDateTime::fromMSecsSinceEpoch(now).toString("yyyyMMddHHmmss"))
        .arg(++m_alarmCounter, 4, 10, QChar('0'));
}

QString AlarmEngine::limitToString(AlarmLimit limit) const {
    switch (limit) {
    case AlarmLimit::HighHigh: return "高高报";
    case AlarmLimit::High:     return "高报";
    case AlarmLimit::Low:      return "低报";
    case AlarmLimit::LowLow:   return "低低报";
    default:                   return "未知";
    }
}
