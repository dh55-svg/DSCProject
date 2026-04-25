#include "AlarmEngine.h"
#include "logger.h"
#include "DatabaseManager.h"
#include "TagConfigMgr.h"
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QDir>
#include <cmath>

// ============================================================
// AlarmEngine
// ============================================================

AlarmEngine& AlarmEngine::instance()
{
    static AlarmEngine instance;
    return instance;
}

AlarmEngine::AlarmEngine()
{
    // On-Delay 检查定时器
    m_onDelayTimer = new QTimer(this);
    m_onDelayTimer->setInterval(500);  // 每500ms检查一次
    connect(m_onDelayTimer, &QTimer::timeout, this, [this]() {
        QMutexLocker lock(&m_mutex);
        qint64 now = QDateTime::currentMSecsSinceEpoch();
        QList<quint32> toTrigger;

        for (auto it = m_onDelayEntries.begin(); it != m_onDelayEntries.end(); ++it) {
            if (it->elapsed.hasExpired(it->onDelayMs)) {
                toTrigger.append(it.key());
            }
        }

        lock.unlock();
        for (quint32 tagId : toTrigger) {
            OnDelayEntry entry;
            {
                QMutexLocker lock2(&m_mutex);
                auto it = m_onDelayEntries.find(tagId);
                if (it == m_onDelayEntries.end()) continue;
                entry = it.value();
                m_onDelayEntries.erase(it);
            }
            onOnDelayTimeout(tagId, entry.limit, entry.value,
                             entry.threshold, entry.priority,
                             entry.classification);
        }
    });

    // Shelve 检查 + KPI 外部统计更新定时器（每10秒）
    m_shelveCheckTimer = new QTimer(this);
    m_shelveCheckTimer->setInterval(10000);
    connect(m_shelveCheckTimer, &QTimer::timeout, this, [this]() {
        QMutexLocker lock(&m_mutex);
        qint64 now = QDateTime::currentMSecsSinceEpoch();
        qint64 staleCutoff = now - static_cast<qint64>(m_kpiMonitor.staleThresholdMin()) * 60 * 1000;

        int totalActive = 0;
        int staleCount  = 0;
        int shelvedCount = 0;

        QList<quint32> toUnshelve;
        for (auto it = m_shelveDeadlines.begin(); it != m_shelveDeadlines.end(); ++it) {
            if (it.value() > 0 && now >= it.value()) {
                toUnshelve.append(it.key());
            }
        }

        // 统计报警状态
        for (const auto& ev : m_activeAlarms) {
            if (ev.shelved) {
                shelvedCount++;
            } else {
                totalActive++;
                // 陈旧报警：未确认且触发超过 30 分钟
                if (ev.state == AlarmState::ActiveUnacknowledged &&
                    ev.triggerTime > 0 && ev.triggerTime < staleCutoff) {
                    staleCount++;
                }
            }
        }

        // 推送外部统计到 KPI 监控器
        m_kpiMonitor.setExternalStats(totalActive, staleCount, shelvedCount);

        lock.unlock();
        for (quint32 tagId : toUnshelve) {
            unshelveAlarm(tagId);
        }
    });
}

AlarmEngine::~AlarmEngine()
{
    m_onDelayTimer->stop();
    m_shelveCheckTimer->stop();
}

void AlarmEngine::initialize()
{
    // 加载报警音效
    m_soundCritical = new QSoundEffect(this);
    m_soundMajor    = new QSoundEffect(this);
    m_soundMinor    = new QSoundEffect(this);

    auto loadSound = [](QSoundEffect* effect, const QString& path, float vol) {
        if (QFile::exists(path)) {
            effect->setSource(QUrl::fromLocalFile(path));
            effect->setVolume(vol);
            return true;
        }
        return false;
    };

    loadSound(m_soundCritical, "./sounds/alarm_critical.wav", 1.0f);
    loadSound(m_soundMajor,    "./sounds/alarm_high.wav",     0.8f);
    loadSound(m_soundMinor,    "./sounds/alarm_low.wav",      0.5f);

    // 启动定时器
    m_onDelayTimer->start();
    m_shelveCheckTimer->start();

    LOG_INFO("AlarmEngine", "ISA-18.2 报警引擎初始化完成 (Level 1-4)");
}

// ============================================================
// Level 1: 报警触发（含 On-Delay）
// ============================================================

void AlarmEngine::triggerAlarm(quint32 tagId, AlarmLimit limit,
                                float triggerValue, float thresholdValue,
                                AlarmPriority priority,
                                AlarmClassification classification,
                                int onDelayMs)
{
    QMutexLocker lock(&m_mutex);

    // 检查是否已屏蔽
    auto activeIt = m_activeAlarms.find(tagId);
    if (activeIt != m_activeAlarms.end() && activeIt->shelved) {
        return;
    }

    // 检查是否已有同 tag 报警
    if (activeIt != m_activeAlarms.end() && activeIt->isActive()) {
        if (limit > activeIt->limit) {
            AlarmLimit oldLimit = activeIt->limit;
            activeIt->limit         = limit;
            activeIt->priority       = priority;
            activeIt->classification = classification;
            activeIt->triggerValue   = triggerValue;
            activeIt->thresholdValue = thresholdValue;
            activeIt->triggerTime    = QDateTime::currentMSecsSinceEpoch();
            activeIt->description    = QString("%1报警升级，当前值=%2，限值=%3")
                                           .arg(limitToString(limit))
                                           .arg(triggerValue, 0, 'f', 1)
                                           .arg(thresholdValue, 0, 'f', 1);

            activeIt->state = AlarmState::ActiveUnacknowledged;
            activeIt->acknowledged = false;
            activeIt->acknowledgeTime = 0;

            lock.unlock();
            playAlarmSound(priority);
            emit alarmTriggered(*activeIt);
            emit alarmEscalated(tagId, oldLimit, limit);
            emit alarmCountChanged(activeAlarmCount(), unacknowledgedCount());
            LOG_WARN("AlarmEngine", QString("报警升级: tagId=%1, %2→%3")
                         .arg(tagId)
                         .arg(static_cast<int>(oldLimit))
                         .arg(static_cast<int>(limit)));
            return;
        }
        return;
    }

    if (activeIt != m_activeAlarms.end() &&
        (activeIt->state == AlarmState::ReturnToNormalUnacknowledged ||
         activeIt->state == AlarmState::ReturnToNormalAcknowledged)) {
        activeIt->state         = AlarmState::ActiveUnacknowledged;
        activeIt->limit          = limit;
        activeIt->priority       = priority;
        activeIt->classification = classification;
        activeIt->triggerValue   = triggerValue;
        activeIt->thresholdValue = thresholdValue;
        activeIt->triggerTime    = QDateTime::currentMSecsSinceEpoch();
        activeIt->acknowledged   = false;
        activeIt->acknowledgeTime = 0;
        activeIt->returnToNormalTime = 0;
        activeIt->returnAckTime  = 0;
        activeIt->description    = QString("%1报警，当前值=%2，限值=%3")
                                       .arg(limitToString(limit))
                                       .arg(triggerValue, 0, 'f', 1)
                                       .arg(thresholdValue, 0, 'f', 1);

        lock.unlock();
        playAlarmSound(priority);
        emit alarmTriggered(*activeIt);
        emit alarmCountChanged(activeAlarmCount(), unacknowledgedCount());
        return;
    }

    // === 新报警：先进入 On-Delay ===
    auto delayIt = m_onDelayEntries.find(tagId);
    if (delayIt != m_onDelayEntries.end()) {
        if (limit > delayIt->limit) {
            delayIt->limit    = limit;
            delayIt->value     = triggerValue;
            delayIt->threshold = thresholdValue;
            delayIt->priority  = priority;
            delayIt->classification = classification;
        }
        delayIt->value = triggerValue;
        return;
    }

    OnDelayEntry entry;
    entry.limit    = limit;
    entry.value     = triggerValue;
    entry.threshold = thresholdValue;
    entry.priority  = priority;
    entry.classification = classification;
    entry.elapsed.start();
    m_onDelayEntries.insert(tagId, entry);

    LOG_DEBUG("AlarmEngine", QString("On-Delay 开始: tagId=%1, limit=%2, value=%3")
                  .arg(tagId)
                  .arg(static_cast<int>(limit))
                  .arg(triggerValue, 0, 'f', 1));
}

void AlarmEngine::onOnDelayTimeout(quint32 tagId, AlarmLimit limit,
                                    float value, float threshold,
                                    AlarmPriority priority,
                                    AlarmClassification classification)
{
    QMutexLocker lock(&m_mutex);

    auto activeIt = m_activeAlarms.find(tagId);
    if (activeIt != m_activeAlarms.end() && activeIt->isActive()) {
        return;
    }

    // 查询位号名
    QString tagName;
    TagInfo tagInfo = TagConfigMgr::instance().getTag(tagId);
    tagName = tagInfo.tagName;

    // 创建报警事件
    AlarmEvent event;
    event.alarmId  = generateAlarmId();
    event.tagId    = tagId;
    event.tagName  = tagName;
    event.limit     = limit;
    event.priority  = priority;
    event.classification = classification;
    event.triggerValue   = value;
    event.thresholdValue = threshold;
    event.triggerTime    = QDateTime::currentMSecsSinceEpoch();
    event.state          = AlarmState::ActiveUnacknowledged;
    event.description    = QString("%1报警，当前值=%2，限值=%3")
                               .arg(limitToString(limit))
                               .arg(value, 0, 'f', 1)
                               .arg(threshold, 0, 'f', 1);

    m_activeAlarms.insert(tagId, event);

    m_alarmHistory.prepend(event);
    if (m_alarmHistory.size() > 2000) {
        m_alarmHistory.removeLast();
    }

    DatabaseManager::instance().insertAlarmRecord(
        tagId, static_cast<int>(limit), event.description,
        value, threshold, event.triggerTime);

    // KPI 记录（带位号名用于 top5 统计）
    m_kpiMonitor.recordAlarm(tagName);

    lock.unlock();

    playAlarmSound(priority);

    emit alarmTriggered(event);
    emit alarmCountChanged(activeAlarmCount(), unacknowledgedCount());

    LOG_WARN("AlarmEngine", QString("报警触发: tagId=%1, %2, 值=%3, onDelay生效")
                 .arg(tagId)
                 .arg(limitToString(limit))
                 .arg(value, 0, 'f', 1));
}

// ============================================================
// Level 1: 值回正常 (Return-to-Normal)
// ============================================================

void AlarmEngine::clearAlarm(quint32 tagId, float returnValue)
{
    QMutexLocker lock(&m_mutex);

    m_onDelayEntries.remove(tagId);

    auto it = m_activeAlarms.find(tagId);
    if (it == m_activeAlarms.end()) return;

    if (!it->isActive()) return;

    it->state = AlarmState::ReturnToNormalUnacknowledged;
    it->returnToNormalTime = QDateTime::currentMSecsSinceEpoch();
    it->returnValue = returnValue;
    it->active = false;

    QString alarmId = it->alarmId;
    lock.unlock();

    emit alarmCountChanged(activeAlarmCount(), unacknowledgedCount());
    LOG_INFO("AlarmEngine", QString("报警恢复: tagId=%1, 值=%2 (等待确认)")
                 .arg(tagId)
                 .arg(returnValue, 0, 'f', 1));
}

// ============================================================
// Level 1: 确认操作
// ============================================================

void AlarmEngine::acknowledgeAlarm(const QString& alarmId)
{
    QMutexLocker lock(&m_mutex);

    for (auto it = m_activeAlarms.begin(); it != m_activeAlarms.end(); ++it) {
        if (it->alarmId == alarmId) {
            if (it->state == AlarmState::ActiveUnacknowledged) {
                it->state = AlarmState::ActiveAcknowledged;
                it->acknowledgeTime = QDateTime::currentMSecsSinceEpoch();
                it->acknowledged = true;

                QString id = alarmId;
                lock.unlock();
                emit alarmAcknowledged(id);
                emit alarmCountChanged(activeAlarmCount(), unacknowledgedCount());
                LOG_INFO("AlarmEngine", QString("报警已确认: %1").arg(id));
            }
            return;
        }
    }
}

void AlarmEngine::acknowledgeAlarmByTagId(quint32 tagId)
{
    QMutexLocker lock(&m_mutex);
    auto it = m_activeAlarms.find(tagId);
    if (it == m_activeAlarms.end()) return;

    if (it->state == AlarmState::ActiveUnacknowledged) {
        it->state = AlarmState::ActiveAcknowledged;
        it->acknowledgeTime = QDateTime::currentMSecsSinceEpoch();
        it->acknowledged = true;
        QString alarmId = it->alarmId;
        lock.unlock();
        emit alarmAcknowledged(alarmId);
        emit alarmCountChanged(activeAlarmCount(), unacknowledgedCount());
        LOG_INFO("AlarmEngine", QString("报警已确认(按位号): tagId=%1").arg(tagId));
    }
}

void AlarmEngine::acknowledgeAll()
{
    QMutexLocker lock(&m_mutex);
    QList<QString> acked;

    for (auto it = m_activeAlarms.begin(); it != m_activeAlarms.end(); ++it) {
        if (it->state == AlarmState::ActiveUnacknowledged) {
            it->state = AlarmState::ActiveAcknowledged;
            it->acknowledgeTime = QDateTime::currentMSecsSinceEpoch();
            it->acknowledged = true;
            acked.append(it->alarmId);
        }
    }
    lock.unlock();

    for (const auto& id : acked) {
        emit alarmAcknowledged(id);
    }
    emit alarmCountChanged(activeAlarmCount(), unacknowledgedCount());
    LOG_INFO("AlarmEngine", QString("所有报警已确认 (%1 条)").arg(acked.size()));
}

// ============================================================
// Level 1: 恢复确认
// ============================================================

void AlarmEngine::acknowledgeReturnToNormal(const QString& alarmId)
{
    QMutexLocker lock(&m_mutex);

    for (auto it = m_activeAlarms.begin(); it != m_activeAlarms.end(); ++it) {
        if (it->alarmId == alarmId &&
            it->state == AlarmState::ReturnToNormalUnacknowledged) {
            it->state = AlarmState::ReturnToNormalAcknowledged;
            it->returnAckTime = QDateTime::currentMSecsSinceEpoch();

            QString id = alarmId;
            m_activeAlarms.erase(it);
            lock.unlock();
            emit alarmReturnToNormalAcknowledged(id);
            emit alarmCleared(id);
            emit alarmCountChanged(activeAlarmCount(), unacknowledgedCount());
            LOG_INFO("AlarmEngine", QString("报警恢复已确认，已关闭: %1").arg(id));
            return;
        }
    }
}

void AlarmEngine::acknowledgeReturnToNormalByTagId(quint32 tagId)
{
    QMutexLocker lock(&m_mutex);
    auto it = m_activeAlarms.find(tagId);
    if (it == m_activeAlarms.end()) return;
    if (it->state != AlarmState::ReturnToNormalUnacknowledged) return;

    QString alarmId = it->alarmId;
    m_activeAlarms.erase(it);
    lock.unlock();

    emit alarmReturnToNormalAcknowledged(alarmId);
    emit alarmCleared(alarmId);
    emit alarmCountChanged(activeAlarmCount(), unacknowledgedCount());
}

void AlarmEngine::acknowledgeAllReturnToNormal()
{
    QMutexLocker lock(&m_mutex);
    QList<QString> cleared;

    auto it = m_activeAlarms.begin();
    while (it != m_activeAlarms.end()) {
        if (it->state == AlarmState::ReturnToNormalUnacknowledged) {
            cleared.append(it->alarmId);
            it = m_activeAlarms.erase(it);
        } else {
            ++it;
        }
    }
    lock.unlock();

    for (const auto& id : cleared) {
        emit alarmReturnToNormalAcknowledged(id);
        emit alarmCleared(id);
    }
    emit alarmCountChanged(activeAlarmCount(), unacknowledgedCount());
}

// ============================================================
// Level 2: Shelving
// ============================================================

void AlarmEngine::shelveAlarm(quint32 tagId, const QString& reason, int durationSec)
{
    QMutexLocker lock(&m_mutex);
    auto it = m_activeAlarms.find(tagId);
    if (it == m_activeAlarms.end()) return;

    it->shelved = true;
    it->shelvedTime = QDateTime::currentMSecsSinceEpoch();
    it->shelveReason = reason;
    it->shelveDurationSec = durationSec;
    it->state = AlarmState::Shelved;

    if (durationSec > 0) {
        m_shelveDeadlines[tagId] = QDateTime::currentMSecsSinceEpoch()
                                   + static_cast<qint64>(durationSec) * 1000;
    } else {
        m_shelveDeadlines.remove(tagId);
    }

    lock.unlock();
    emit alarmShelved(tagId, reason, durationSec);
    emit alarmCountChanged(activeAlarmCount(), unacknowledgedCount());
    LOG_INFO("AlarmEngine", QString("报警已屏蔽: tagId=%1, 原因=%2, 时长=%3s")
                 .arg(tagId).arg(reason).arg(durationSec));
}

void AlarmEngine::unshelveAlarm(quint32 tagId)
{
    QMutexLocker lock(&m_mutex);
    m_shelveDeadlines.remove(tagId);

    auto it = m_activeAlarms.find(tagId);
    if (it == m_activeAlarms.end()) return;
    if (!it->shelved) return;

    it->shelved = false;
    it->shelvedTime = 0;
    it->shelveReason.clear();

    if (it->isActive()) {
        it->state = AlarmState::ActiveUnacknowledged;
    } else {
        it->state = AlarmState::ReturnToNormalUnacknowledged;
    }

    lock.unlock();
    emit alarmUnshelved(tagId);
    emit alarmCountChanged(activeAlarmCount(), unacknowledgedCount());
    LOG_INFO("AlarmEngine", QString("报警取消屏蔽: tagId=%1").arg(tagId));
}

QList<AlarmEvent> AlarmEngine::shelvedAlarms() const
{
    QMutexLocker lock(&m_mutex);
    QList<AlarmEvent> result;
    for (const auto& event : m_activeAlarms) {
        if (event.shelved) result.append(event);
    }
    return result;
}

// ============================================================
// Level 2+4: 报警参数修改
// ============================================================

bool AlarmEngine::setAlarmLimit(quint32 tagId, const QString& fieldName,
                                 float newValue, const QString& operatorName,
                                 const QString& reason)
{
    // 获取旧值
    QString oldValue;
    TagInfo tag = TagConfigMgr::instance().getTag(tagId);
    if (fieldName == "highHighLimit") oldValue = QString::number(tag.highHighLimit, 'f', 1);
    else if (fieldName == "highLimit")    oldValue = QString::number(tag.highLimit, 'f', 1);
    else if (fieldName == "lowLimit")     oldValue = QString::number(tag.lowLimit, 'f', 1);
    else if (fieldName == "lowLowLimit")  oldValue = QString::number(tag.lowLowLimit, 'f', 1);
    else if (fieldName == "deadband")     oldValue = QString::number(tag.deadband, 'f', 1);
    else if (fieldName == "onDelayMs")    oldValue = QString::number(tag.onDelayMs);

    AlarmChangeRecord rec;
    rec.tagId       = tagId;
    rec.fieldName   = fieldName;
    rec.oldValue    = oldValue;
    rec.newValue    = QString::number(newValue, 'f', 1);
    rec.operatorName = operatorName;
    rec.reason      = reason;

    m_changeLog.recordChange(rec);

    emit alarmParameterChanged(tagId, fieldName, rec.oldValue, rec.newValue);
    emit changeRecorded(rec);
    return true;
}

bool AlarmEngine::setAlarmPriority(quint32 tagId, AlarmPriority newPriority,
                                    const QString& operatorName,
                                    const QString& reason)
{
    TagInfo tag = TagConfigMgr::instance().getTag(tagId);
    QString oldVal = QString::number(static_cast<int>(tag.priority));

    AlarmChangeRecord rec;
    rec.tagId       = tagId;
    rec.fieldName   = "priority";
    rec.oldValue    = oldVal;
    rec.newValue    = QString::number(static_cast<int>(newPriority));
    rec.operatorName = operatorName;
    rec.reason      = reason;

    m_changeLog.recordChange(rec);
    emit alarmParameterChanged(tagId, "priority", rec.oldValue, rec.newValue);
    emit changeRecorded(rec);
    return true;
}

// ============================================================
// Level 1-2: 查询接口
// ============================================================

QList<AlarmEvent> AlarmEngine::activeAlarms() const
{
    QMutexLocker lock(&m_mutex);
    QList<AlarmEvent> result;
    for (const auto& event : m_activeAlarms) {
        if (!event.shelved) result.append(event);
    }
    return result;
}

QList<AlarmEvent> AlarmEngine::unacknowledgedAlarms() const
{
    QMutexLocker lock(&m_mutex);
    QList<AlarmEvent> result;
    for (const auto& event : m_activeAlarms) {
        if (event.needsAttention()) result.append(event);
    }
    return result;
}

AlarmEvent AlarmEngine::alarmByTagId(quint32 tagId) const
{
    QMutexLocker lock(&m_mutex);
    auto it = m_activeAlarms.find(tagId);
    if (it != m_activeAlarms.end()) return *it;
    return AlarmEvent();
}

QList<AlarmEvent> AlarmEngine::alarmHistory(int limit) const
{
    QMutexLocker lock(&m_mutex);
    return m_alarmHistory.mid(0, qMin(limit, m_alarmHistory.size()));
}

int AlarmEngine::activeAlarmCount() const
{
    QMutexLocker lock(&m_mutex);
    int count = 0;
    for (const auto& event : m_activeAlarms) {
        if (!event.shelved) count++;
    }
    return count;
}

int AlarmEngine::activeAlarmCount(AlarmLimit limit) const
{
    QMutexLocker lock(&m_mutex);
    int count = 0;
    for (const auto& event : m_activeAlarms) {
        if (!event.shelved && event.limit == limit) count++;
    }
    return count;
}

int AlarmEngine::activeAlarmCount(AlarmPriority priority) const
{
    QMutexLocker lock(&m_mutex);
    int count = 0;
    for (const auto& event : m_activeAlarms) {
        if (!event.shelved && event.priority == priority) count++;
    }
    return count;
}

int AlarmEngine::unacknowledgedCount() const
{
    QMutexLocker lock(&m_mutex);
    int count = 0;
    for (const auto& event : m_activeAlarms) {
        if (event.needsAttention()) count++;
    }
    return count;
}

// ============================================================
// 音频控制
// ============================================================

void AlarmEngine::setSoundEnabled(bool enabled)
{
    m_soundEnabled = enabled;
    if (!enabled) {
        if (m_soundCritical) m_soundCritical->stop();
        if (m_soundMajor)    m_soundMajor->stop();
        if (m_soundMinor)    m_soundMinor->stop();
    }
}

void AlarmEngine::playAlarmSound(AlarmPriority priority)
{
    if (!m_soundEnabled) return;

    QSoundEffect* effect = nullptr;
    switch (priority) {
    case AlarmPriority::Critical: effect = m_soundCritical; break;
    case AlarmPriority::Major:    effect = m_soundMajor;    break;
    case AlarmPriority::Minor:
    case AlarmPriority::Advisory: effect = m_soundMinor;    break;
    }

    if (effect) effect->play();
}

// ============================================================
// 内部辅助
// ============================================================

QString AlarmEngine::generateAlarmId()
{
    qint64 now = QDateTime::currentMSecsSinceEpoch();
    return QString("ALM_%1_%2")
        .arg(QDateTime::fromMSecsSinceEpoch(now).toString("yyyyMMddHHmmss"))
        .arg(++m_alarmCounter, 4, 10, QChar('0'));
}

QString AlarmEngine::limitToString(AlarmLimit limit) const
{
    switch (limit) {
    case AlarmLimit::HighHigh: return "高高报";
    case AlarmLimit::High:     return "高报";
    case AlarmLimit::Low:      return "低报";
    case AlarmLimit::LowLow:   return "低低报";
    default:                   return "未知";
    }
}

QString AlarmEngine::soundPathForPriority(AlarmPriority priority) const
{
    switch (priority) {
    case AlarmPriority::Critical: return "./sounds/alarm_critical.wav";
    case AlarmPriority::Major:    return "./sounds/alarm_high.wav";
    case AlarmPriority::Minor:    return "./sounds/alarm_low.wav";
    default:                      return "";
    }
}
