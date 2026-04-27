#include "AlarmEngine.h"
#include "logger.h"
#include "DatabaseManager.h"
#include "TagConfigMgr.h"
#include "AuthManager.h"
#include "DoubleBuffer.h"
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QDir>
#include <cmath>
#include <algorithm>

// ============================================================
// AlarmEngine - ISA-18.2 商业化报警引擎完整实现
// ============================================================

AlarmEngine& AlarmEngine::instance()
{
    static AlarmEngine instance;
    return instance;
}

AlarmEngine::AlarmEngine()
{
    // === On-Delay 检查定时器（每500ms检查一次） ===
    m_onDelayTimer = new QTimer(this);
    m_onDelayTimer->setInterval(500);
    connect(m_onDelayTimer, &QTimer::timeout, this, [this]() {
        QMutexLocker lock(&m_mutex);
        qint64 now = QDateTime::currentMSecsSinceEpoch();
        QList<quint32> toTrigger;

        // 遍历所有 On-Delay 条目，检查是否到期
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

    // === Off-Delay 检查定时器（每500ms检查一次） ===
    m_offDelayTimer = new QTimer(this);
    m_offDelayTimer->setInterval(500);
    connect(m_offDelayTimer, &QTimer::timeout, this, [this]() {
        QMutexLocker lock(&m_mutex);
        QList<quint32> toClear;

        // 遍历所有 Off-Delay 条目，检查是否到期
        for (auto it = m_offDelayEntries.begin(); it != m_offDelayEntries.end(); ++it) {
            if (it->elapsed.hasExpired(it->offDelayMs)) {
                toClear.append(it.key());
            }
        }

        lock.unlock();
        for (quint32 tagId : toClear) {
            OffDelayEntry entry;
            {
                QMutexLocker lock2(&m_mutex);
                auto it = m_offDelayEntries.find(tagId);
                if (it == m_offDelayEntries.end()) continue;
                entry = it.value();
                m_offDelayEntries.erase(it);
            }
            onOffDelayTimeout(tagId, entry.returnValue);
        }
    });

    // === Shelve 到期检查 + KPI 外部统计更新定时器（每10秒） ===
    m_shelveCheckTimer = new QTimer(this);
    m_shelveCheckTimer->setInterval(10000);
    connect(m_shelveCheckTimer, &QTimer::timeout, this, [this]() {
        QMutexLocker lock(&m_mutex);
        qint64 now = QDateTime::currentMSecsSinceEpoch();
        qint64 staleCutoff = now - static_cast<qint64>(m_kpiMonitor.staleThresholdMin()) * 60 * 1000;

        int totalActive = 0;
        int staleCount  = 0;
        int shelvedCount = 0;
        int suppressedCount = 0;

        // 检查屏蔽到期
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
            } else if (ev.isSuppressed()) {
                suppressedCount++;
            } else {
                totalActive++;
                // 陈旧报警：未确认且触发超过 staleThresholdMin 分钟
                if (ev.state == AlarmState::ActiveUnacknowledged &&
                    ev.triggerTime > 0 && ev.triggerTime < staleCutoff) {
                    staleCount++;
                }
            }
        }

        // 推送外部统计到 KPI 监控器
        m_kpiMonitor.setExternalStats(totalActive, staleCount, shelvedCount);

        // KPI 持久化：每5分钟保存一次快照到数据库（不足7修复）
        bool shouldSaveKpi = (m_lastKpiSaveTime == 0 || (now - m_lastKpiSaveTime) >= 300000);
        AlarmKpiSnapshot kpiSnap;
        if (shouldSaveKpi) {
            m_lastKpiSaveTime = now;
            kpiSnap = m_kpiMonitor.snapshot(totalActive, staleCount, shelvedCount);
            kpiSnap.timestamp = now;
        }

        lock.unlock();

        // 在锁外保存 KPI 快照（避免数据库 I/O 阻塞）
        if (shouldSaveKpi) {
            DatabaseManager::instance().insertKpiSnapshot(kpiSnap);
        }

        // 自动取消到期屏蔽
        for (quint32 tagId : toUnshelve) {
            unshelveAlarm(tagId);
        }

        // 检测报警泛滥
        checkFloodCondition();
    });
}

AlarmEngine::~AlarmEngine()
{
    m_onDelayTimer->stop();
    m_offDelayTimer->stop();
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

    // 启动所有定时器
    m_onDelayTimer->start();
    m_offDelayTimer->start();
    m_shelveCheckTimer->start();

    LOG_INFO("AlarmEngine", "ISA-18.2 报警引擎初始化完成 (Level 1-4 + 商业化增强)");
}

// ============================================================
// Level 1: 报警触发（含完整 ISA-18.2 逻辑链）
// ============================================================

void AlarmEngine::triggerAlarm(quint32 tagId, AlarmLimit limit,
                                float triggerValue, float thresholdValue,
                                AlarmPriority priority,
                                AlarmClassification classification,
                                int onDelayMs)
{
    QMutexLocker lock(&m_mutex);

    // === 步骤1: 检查报警是否启用 ===
    TagInfo tagInfo = TagConfigMgr::instance().getTag(tagId);
    if (!tagInfo.alarmEnabled) {
        LOG_DEBUG("AlarmEngine", QString("报警已禁用: tagId=%1").arg(tagId));
        return;
    }
    if (!tagInfo.isLimitEnabled(limit)) {
        LOG_DEBUG("AlarmEngine", QString("限值等级已禁用: tagId=%1, limit=%2")
                      .arg(tagId).arg(static_cast<int>(limit)));
        return;
    }

    // === 步骤2: 检查是否被抑制（SuppressedByDesign / OutOfService） ===
    auto activeIt = m_activeAlarms.find(tagId);
    if (activeIt != m_activeAlarms.end()) {
        if (activeIt->state == AlarmState::SuppressedByDesign) {
            LOG_DEBUG("AlarmEngine", QString("报警被设计抑制，忽略: tagId=%1").arg(tagId));
            return;
        }
        if (activeIt->state == AlarmState::OutOfService) {
            LOG_DEBUG("AlarmEngine", QString("报警已停用，忽略: tagId=%1").arg(tagId));
            return;
        }
    }

    // === 步骤3: 检查是否被屏蔽（Shelved） ===
    if (activeIt != m_activeAlarms.end() && activeIt->shelved) {
        // 屏蔽中的报警仍然记录 repeatCount，用于 Chattering 检测
        activeIt->repeatCount++;
        LOG_DEBUG("AlarmEngine", QString("报警已屏蔽，记录重复: tagId=%1, repeat=%2")
                      .arg(tagId).arg(activeIt->repeatCount));
        return;
    }

    // === 步骤4: 检查条件抑制规则（Suppression-by-Condition — 不足1修复） ===
    if (evaluateSuppression(tagId)) {
        LOG_DEBUG("AlarmEngine", QString("报警被条件抑制规则抑制: tagId=%1").arg(tagId));
        return;
    }

    // === 步骤5: 检查重复报警保护（Chattering Protection） ===
    if (checkChattering(tagId)) {
        LOG_WARN("AlarmEngine", QString("Chattering 保护触发，自动屏蔽: tagId=%1").arg(tagId));
        return;
    }

    // === 步骤6: 检查是否已有同 tag 报警 → 升级/去重 ===
    if (activeIt != m_activeAlarms.end() && activeIt->isActive()) {
        // 报警升级：新限值更严重时升级
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
            activeIt->repeatCount++;

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
        // 同级别或更低级别：去重，不重复触发
        activeIt->repeatCount++;
        return;
    }

    // === 报警已恢复正常但未确认，现在又重新触发 ===
    if (activeIt != m_activeAlarms.end() &&
        (activeIt->state == AlarmState::ReturnToNormalUnacknowledged ||
         activeIt->state == AlarmState::ReturnToNormalAcknowledged)) {
        // 取消 Off-Delay 恢复等待（如果有）
        m_offDelayEntries.remove(tagId);

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
        activeIt->active         = true;
        activeIt->description    = QString("%1报警，当前值=%2，限值=%3")
                                       .arg(limitToString(limit))
                                       .arg(triggerValue, 0, 'f', 1)
                                       .arg(thresholdValue, 0, 'f', 1);
        activeIt->repeatCount++;

        lock.unlock();
        playAlarmSound(priority);
        emit alarmTriggered(*activeIt);
        emit alarmCountChanged(activeAlarmCount(), unacknowledgedCount());
        return;
    }

    // === 步骤7: 新报警 → 先进入 On-Delay 等待 ===
    auto delayIt = m_onDelayEntries.find(tagId);
    if (delayIt != m_onDelayEntries.end()) {
        // 已在 On-Delay 中，如果新限值更严重则升级
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
    entry.limit          = limit;
    entry.value          = triggerValue;
    entry.threshold      = thresholdValue;
    entry.priority       = priority;
    entry.classification = classification;
    entry.onDelayMs      = onDelayMs;
    entry.elapsed.start();
    m_onDelayEntries.insert(tagId, entry);

    LOG_DEBUG("AlarmEngine", QString("On-Delay 开始: tagId=%1, limit=%2, value=%3, delay=%4ms")
                  .arg(tagId)
                  .arg(static_cast<int>(limit))
                  .arg(triggerValue, 0, 'f', 1)
                  .arg(onDelayMs));
}

void AlarmEngine::onOnDelayTimeout(quint32 tagId, AlarmLimit limit,
                                    float value, float threshold,
                                    AlarmPriority priority,
                                    AlarmClassification classification)
{
    QMutexLocker lock(&m_mutex);

    // 再次检查：On-Delay 期间可能已被确认或恢复
    auto activeIt = m_activeAlarms.find(tagId);
    if (activeIt != m_activeAlarms.end() && activeIt->isActive()) {
        return;
    }

    // 查询位号名和区域信息
    QString tagName;
    QString area;
    QString zone;
    TagInfo tagInfo = TagConfigMgr::instance().getTag(tagId);
    tagName = tagInfo.tagName;
    area    = tagInfo.area;
    zone    = tagInfo.zone;

    // === 创建报警事件 ===
    AlarmEvent event;
    event.alarmId        = generateAlarmId();
    event.tagId          = tagId;
    event.tagName        = tagName;
    event.limit          = limit;
    event.priority       = priority;
    event.classification = classification;
    event.triggerValue   = value;
    event.thresholdValue = threshold;
    event.triggerTime    = QDateTime::currentMSecsSinceEpoch();
    event.firstTriggerTime = event.triggerTime;
    event.state          = AlarmState::ActiveUnacknowledged;
    event.description    = QString("%1报警，当前值=%2，限值=%3")
                               .arg(limitToString(limit))
                               .arg(value, 0, 'f', 1)
                               .arg(threshold, 0, 'f', 1);
    event.area           = area;
    event.zone           = zone;
    event.notificationType = tagInfo.notificationType;
    event.repeatCount    = 1;

    m_activeAlarms.insert(tagId, event);

    // 添加到报警历史
    m_alarmHistory.prepend(event);
    if (m_alarmHistory.size() > 5000) {
        m_alarmHistory.removeLast();
    }

    // 持久化到数据库
    DatabaseManager::instance().insertAlarmRecord(
        tagId, static_cast<int>(limit), event.description,
        value, threshold, event.triggerTime);

    // KPI 记录（带位号名用于 top5 统计）
    m_kpiMonitor.recordAlarm(tagName);

    // 更新 Chattering 计数
    auto& chatState = m_chatteringState[tagId];
    qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (chatState.windowStart == 0 || (now - chatState.windowStart) > 60000) {
        chatState.count = 1;
        chatState.windowStart = now;
        chatState.autoShelved = false;
    } else {
        chatState.count++;
    }

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
// Level 1: 值回正常（含 Off-Delay 支持）
// ============================================================

void AlarmEngine::clearAlarm(quint32 tagId, float returnValue)
{
    QMutexLocker lock(&m_mutex);

    // 取消 On-Delay 等待（值已回正常，不需要再触发）
    m_onDelayEntries.remove(tagId);

    auto it = m_activeAlarms.find(tagId);
    if (it == m_activeAlarms.end()) return;

    if (!it->isActive()) return;

    // === 查询位号的 Off-Delay 配置 ===
    TagInfo tagInfo = TagConfigMgr::instance().getTag(tagId);
    int offDelayMs = tagInfo.offDelayMs;

    if (offDelayMs > 0) {
        // Off-Delay: 值回正常后不立即恢复，需要持续正常 offDelayMs 毫秒
        OffDelayEntry offEntry;
        offEntry.returnValue = returnValue;
        offEntry.offDelayMs  = offDelayMs;
        offEntry.elapsed.start();
        m_offDelayEntries.insert(tagId, offEntry);

        LOG_DEBUG("AlarmEngine", QString("Off-Delay 开始: tagId=%1, returnValue=%2, delay=%3ms")
                      .arg(tagId)
                      .arg(returnValue, 0, 'f', 1)
                      .arg(offDelayMs));
        return;
    }

    // 无 Off-Delay，立即恢复
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

void AlarmEngine::onOffDelayTimeout(quint32 tagId, float returnValue)
{
    QMutexLocker lock(&m_mutex);

    auto it = m_activeAlarms.find(tagId);
    if (it == m_activeAlarms.end()) return;

    // Off-Delay 期间可能又超限了，此时不再恢复
    if (it->isActive()) {
        LOG_DEBUG("AlarmEngine", QString("Off-Delay 期间重新超限，取消恢复: tagId=%1").arg(tagId));
        return;
    }

    // 确认恢复
    it->state = AlarmState::ReturnToNormalUnacknowledged;
    it->returnToNormalTime = QDateTime::currentMSecsSinceEpoch();
    it->returnValue = returnValue;
    it->active = false;

    lock.unlock();

    emit alarmCountChanged(activeAlarmCount(), unacknowledgedCount());
    LOG_INFO("AlarmEngine", QString("报警恢复(Off-Delay确认): tagId=%1, 值=%2")
                 .arg(tagId)
                 .arg(returnValue, 0, 'f', 1));
}

// ============================================================
// Level 1: 确认操作
// ============================================================

void AlarmEngine::acknowledgeAlarm(const QString& alarmId)
{
    // 向后兼容：使用当前登录用户
    QString user = AuthManager::instance().currentUsername();
    acknowledgeAlarm(alarmId, user);
}

bool AlarmEngine::acknowledgeAlarm(const QString& alarmId, const QString& operatorName)
{
    // 权限检查（ISA-18.2 要求确认报警必须有操作员以上权限）
    if (!AuthManager::instance().canOperate()) {
        emit AuthManager::instance().permissionDenied(QStringLiteral("确认报警"));
        LOG_WARN("AlarmEngine", QString("权限不足: %1 尝试确认报警 %2").arg(operatorName).arg(alarmId));
        return false;
    }

    QMutexLocker lock(&m_mutex);

    for (auto it = m_activeAlarms.begin(); it != m_activeAlarms.end(); ++it) {
        if (it->alarmId == alarmId) {
            if (it->state == AlarmState::ActiveUnacknowledged) {
                it->state = AlarmState::ActiveAcknowledged;
                it->acknowledgeTime = QDateTime::currentMSecsSinceEpoch();
                it->acknowledged = true;
                it->acknowledgeUser = operatorName;

                QString id = alarmId;
                qint64 ackTime = it->acknowledgeTime;
                lock.unlock();
                emit alarmAcknowledged(id);
                emit alarmCountChanged(activeAlarmCount(), unacknowledgedCount());
                DatabaseManager::instance().updateAlarmEvent(id, "acknowledge", operatorName, ackTime);
                LOG_INFO("AlarmEngine", QString("报警已确认: %1, 操作员=%2").arg(id).arg(operatorName));
            }
            return true;
        }
    }
    return false;
}

void AlarmEngine::acknowledgeAlarmByTagId(quint32 tagId)
{
    QString user = AuthManager::instance().currentUsername();
    acknowledgeAlarmByTagId(tagId, user);
}

bool AlarmEngine::acknowledgeAlarmByTagId(quint32 tagId, const QString& operatorName)
{
    if (!AuthManager::instance().canOperate()) {
        emit AuthManager::instance().permissionDenied(QStringLiteral("确认报警"));
        LOG_WARN("AlarmEngine", QString("权限不足: %1 尝试确认报警 tagId=%2").arg(operatorName).arg(tagId));
        return false;
    }

    QMutexLocker lock(&m_mutex);
    auto it = m_activeAlarms.find(tagId);
    if (it == m_activeAlarms.end()) return false;

    if (it->state == AlarmState::ActiveUnacknowledged) {
        it->state = AlarmState::ActiveAcknowledged;
        it->acknowledgeTime = QDateTime::currentMSecsSinceEpoch();
        it->acknowledged = true;
        it->acknowledgeUser = operatorName;
        QString alarmId = it->alarmId;
        qint64 ackTime = it->acknowledgeTime;
        lock.unlock();
        emit alarmAcknowledged(alarmId);
        emit alarmCountChanged(activeAlarmCount(), unacknowledgedCount());
        DatabaseManager::instance().updateAlarmEvent(alarmId, "acknowledge", operatorName, ackTime);
        LOG_INFO("AlarmEngine", QString("报警已确认(按位号): tagId=%1, 操作员=%2").arg(tagId).arg(operatorName));
        return true;
    }
    return false;
}

void AlarmEngine::acknowledgeAll()
{
    QString user = AuthManager::instance().currentUsername();
    acknowledgeAll(user);
}

void AlarmEngine::acknowledgeAll(const QString& operatorName)
{
    if (!AuthManager::instance().canOperate()) {
        emit AuthManager::instance().permissionDenied(QStringLiteral("确认所有报警"));
        LOG_WARN("AlarmEngine", QString("权限不足: %1 尝试确认所有报警").arg(operatorName));
        return;
    }

    QMutexLocker lock(&m_mutex);
    QList<QPair<QString, qint64>> acked;

    for (auto it = m_activeAlarms.begin(); it != m_activeAlarms.end(); ++it) {
        if (it->state == AlarmState::ActiveUnacknowledged) {
            it->state = AlarmState::ActiveAcknowledged;
            it->acknowledgeTime = QDateTime::currentMSecsSinceEpoch();
            it->acknowledged = true;
            it->acknowledgeUser = operatorName;
            acked.append({it->alarmId, it->acknowledgeTime});
        }
    }
    lock.unlock();

    for (const auto& [id, ackTime] : acked) {
        emit alarmAcknowledged(id);
        DatabaseManager::instance().updateAlarmEvent(id, "acknowledge", operatorName, ackTime);
    }
    emit alarmCountChanged(activeAlarmCount(), unacknowledgedCount());
    LOG_INFO("AlarmEngine", QString("所有报警已确认 (%1 条), 操作员=%2").arg(acked.size()).arg(operatorName));
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
    LOG_INFO("AlarmEngine", QString("报警恢复已确认(按位号): tagId=%1").arg(tagId));
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
    LOG_INFO("AlarmEngine", QString("所有恢复报警已确认 (%1 条)").arg(cleared.size()));
}

// ============================================================
// Level 2: Shelving（操作员临时屏蔽）
// ============================================================

void AlarmEngine::shelveAlarm(quint32 tagId, const QString& reason,
                               int durationSec, const QString& user)
{
    QMutexLocker lock(&m_mutex);

    // 原因必须填写（ISA-18.2 要求）
    if (reason.trimmed().isEmpty()) {
        LOG_WARN("AlarmEngine", QString("屏蔽报警失败: 原因不能为空, tagId=%1").arg(tagId));
        return;
    }

    auto it = m_activeAlarms.find(tagId);

    // 即使没有活跃报警，也记录屏蔽状态（下次触发时自动屏蔽）
    if (it != m_activeAlarms.end()) {
        it->shelved = true;
        it->shelvedTime = QDateTime::currentMSecsSinceEpoch();
        it->shelveReason = reason;
        it->shelveDurationSec = durationSec;
        it->shelveUser = user;
        it->state = AlarmState::Shelved;
    }

    // 设置屏蔽到期时间
    if (durationSec > 0) {
        m_shelveDeadlines[tagId] = QDateTime::currentMSecsSinceEpoch()
                                   + static_cast<qint64>(durationSec) * 1000;
    } else {
        // 永久屏蔽（直到手动取消）
        m_shelveDeadlines[tagId] = 0;
    }

    // 记录变更日志（ISA-18.2 Level 4）
    AlarmChangeRecord rec;
    rec.changeTime   = QDateTime::currentMSecsSinceEpoch();
    rec.operatorName = user;
    rec.tagId        = tagId;
    rec.fieldName    = "shelve";
    rec.oldValue     = "false";
    rec.newValue     = QString("true (duration=%1s, reason=%2)").arg(durationSec).arg(reason);
    rec.reason       = reason;
    rec.approved     = true;
    rec.approver     = user;
    rec.approveTime  = rec.changeTime;
    m_changeLog.recordChange(rec);

    lock.unlock();

    emit alarmShelved(tagId, reason, durationSec);
    emit alarmCountChanged(activeAlarmCount(), unacknowledgedCount());
    emit changeRecorded(rec);
    LOG_INFO("AlarmEngine", QString("报警已屏蔽: tagId=%1, 原因=%2, 时长=%3s, 操作员=%4")
                 .arg(tagId).arg(reason).arg(durationSec).arg(user));
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
    it->shelveUser.clear();

    // 恢复到屏蔽前的逻辑状态
    if (it->active) {
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
// Level 2: Suppression-by-Design（设计抑制）
// ============================================================

void AlarmEngine::suppressByDesign(quint32 tagId, const QString& reason,
                                    const QString& user, const QString& approver)
{
    QMutexLocker lock(&m_mutex);

    // 原因和审批人必须填写（ISA-18.2 强制要求）
    if (reason.trimmed().isEmpty() || approver.trimmed().isEmpty()) {
        LOG_WARN("AlarmEngine", QString("设计抑制失败: 原因和审批人不能为空, tagId=%1").arg(tagId));
        return;
    }

    auto it = m_activeAlarms.find(tagId);
    if (it != m_activeAlarms.end()) {
        it->state = AlarmState::SuppressedByDesign;
        it->suppressionType = AlarmSuppressionType::DesignSuppression;
        it->suppressionReason = reason;
        it->suppressionUser = user;
        it->suppressionTime = QDateTime::currentMSecsSinceEpoch();
    } else {
        // 没有活跃报警，创建一条抑制记录
        AlarmEvent event;
        event.alarmId = generateAlarmId();
        event.tagId   = tagId;
        TagInfo tagInfo = TagConfigMgr::instance().getTag(tagId);
        event.tagName = tagInfo.tagName;
        event.area    = tagInfo.area;
        event.zone    = tagInfo.zone;
        event.state   = AlarmState::SuppressedByDesign;
        event.suppressionType = AlarmSuppressionType::DesignSuppression;
        event.suppressionReason = reason;
        event.suppressionUser = user;
        event.suppressionTime = QDateTime::currentMSecsSinceEpoch();
        event.active = false;
        m_activeAlarms.insert(tagId, event);
    }

    // 记录变更日志（ISA-18.2 Level 4: 审批流程）
    AlarmChangeRecord rec;
    rec.changeTime   = QDateTime::currentMSecsSinceEpoch();
    rec.operatorName = user;
    rec.tagId        = tagId;
    rec.fieldName    = "suppressionByDesign";
    rec.oldValue     = "false";
    rec.newValue     = QString("true (reason=%1)").arg(reason);
    rec.reason       = reason;
    rec.approved     = true;
    rec.approver     = approver;
    rec.approveTime  = rec.changeTime;
    m_changeLog.recordChange(rec);

    lock.unlock();

    emit alarmSuppressed(tagId, reason);
    emit alarmCountChanged(activeAlarmCount(), unacknowledgedCount());
    emit changeRecorded(rec);
    LOG_INFO("AlarmEngine", QString("报警设计抑制: tagId=%1, 原因=%2, 工程师=%3, 审批人=%4")
                 .arg(tagId).arg(reason).arg(user).arg(approver));
}

void AlarmEngine::unsuppressByDesign(quint32 tagId)
{
    QMutexLocker lock(&m_mutex);

    auto it = m_activeAlarms.find(tagId);
    if (it == m_activeAlarms.end()) return;
    if (it->state != AlarmState::SuppressedByDesign) return;

    // 如果有实际的报警条件，恢复到活跃状态；否则移除
    if (it->triggerTime > 0 && it->active) {
        it->state = AlarmState::ActiveUnacknowledged;
    } else {
        // 无实际报警条件，直接移除抑制记录
        m_activeAlarms.erase(it);
    }

    lock.unlock();

    emit alarmUnsuppressed(tagId);
    emit alarmCountChanged(activeAlarmCount(), unacknowledgedCount());
    LOG_INFO("AlarmEngine", QString("取消设计抑制: tagId=%1").arg(tagId));
}

// ============================================================
// Level 2: Out-of-Service（设备停用）
// ============================================================

void AlarmEngine::setOutOfService(quint32 tagId, const QString& reason,
                                   const QString& user, const QString& workOrderNo)
{
    QMutexLocker lock(&m_mutex);

    // 工单号必须填写（ISA-18.2 商业化要求）
    if (workOrderNo.trimmed().isEmpty()) {
        LOG_WARN("AlarmEngine", QString("停用失败: 工单号不能为空, tagId=%1").arg(tagId));
        return;
    }

    auto it = m_activeAlarms.find(tagId);
    if (it != m_activeAlarms.end()) {
        it->state = AlarmState::OutOfService;
        it->outOfService = true;
        it->outOfServiceReason = reason;
        it->outOfServiceUser = user;
        it->workOrderNo = workOrderNo;
        it->suppressionType = AlarmSuppressionType::OutOfService;
        it->suppressionTime = QDateTime::currentMSecsSinceEpoch();
    } else {
        // 没有活跃报警，创建一条停用记录
        AlarmEvent event;
        event.alarmId = generateAlarmId();
        event.tagId   = tagId;
        TagInfo tagInfo = TagConfigMgr::instance().getTag(tagId);
        event.tagName = tagInfo.tagName;
        event.area    = tagInfo.area;
        event.zone    = tagInfo.zone;
        event.state   = AlarmState::OutOfService;
        event.outOfService = true;
        event.outOfServiceReason = reason;
        event.outOfServiceUser = user;
        event.workOrderNo = workOrderNo;
        event.suppressionType = AlarmSuppressionType::OutOfService;
        event.suppressionTime = QDateTime::currentMSecsSinceEpoch();
        event.active = false;
        m_activeAlarms.insert(tagId, event);
    }

    // 取消 On-Delay（设备停用，不再需要触发延时）
    m_onDelayEntries.remove(tagId);

    // 记录变更日志
    AlarmChangeRecord rec;
    rec.changeTime   = QDateTime::currentMSecsSinceEpoch();
    rec.operatorName = user;
    rec.tagId        = tagId;
    rec.fieldName    = "outOfService";
    rec.oldValue     = "false";
    rec.newValue     = QString("true (workOrder=%1, reason=%2)").arg(workOrderNo).arg(reason);
    rec.reason       = reason;
    rec.approved     = true;
    rec.approver     = user;
    rec.approveTime  = rec.changeTime;
    rec.workOrderNo  = workOrderNo;
    m_changeLog.recordChange(rec);

    lock.unlock();

    emit alarmOutOfService(tagId, reason);
    emit alarmCountChanged(activeAlarmCount(), unacknowledgedCount());
    emit changeRecorded(rec);
    LOG_INFO("AlarmEngine", QString("报警停用: tagId=%1, 原因=%2, 操作员=%3, 工单=%4")
                 .arg(tagId).arg(reason).arg(user).arg(workOrderNo));
}

void AlarmEngine::returnToService(quint32 tagId)
{
    QMutexLocker lock(&m_mutex);

    auto it = m_activeAlarms.find(tagId);
    if (it == m_activeAlarms.end()) return;
    if (it->state != AlarmState::OutOfService) return;

    it->outOfService = false;
    it->outOfServiceReason.clear();
    it->outOfServiceUser.clear();
    it->workOrderNo.clear();
    it->suppressionType = AlarmSuppressionType::None;
    it->suppressionTime = 0;

    // 如果有实际的报警条件，恢复到活跃状态；否则移除
    if (it->triggerTime > 0 && it->active) {
        it->state = AlarmState::ActiveUnacknowledged;
    } else {
        m_activeAlarms.erase(it);
    }

    lock.unlock();

    emit alarmReturnedToService(tagId);
    emit alarmCountChanged(activeAlarmCount(), unacknowledgedCount());
    LOG_INFO("AlarmEngine", QString("报警恢复服务: tagId=%1").arg(tagId));
}

// ============================================================
// Level 2: 操作员注释
// ============================================================

void AlarmEngine::annotateAlarm(const QString& alarmId, const QString& annotation,
                                 const QString& user)
{
    QMutexLocker lock(&m_mutex);

    for (auto it = m_activeAlarms.begin(); it != m_activeAlarms.end(); ++it) {
        if (it->alarmId == alarmId) {
            it->operatorAnnotation = annotation;
            it->annotationTime = QDateTime::currentMSecsSinceEpoch();
            it->annotationUser = user;

            // 记录变更日志
            AlarmChangeRecord rec;
            rec.changeTime   = it->annotationTime;
            rec.operatorName = user;
            rec.tagId        = it->tagId;
            rec.fieldName    = "operatorAnnotation";
            rec.oldValue     = "";
            rec.newValue     = annotation;
            rec.reason       = QString("操作员注释: %1").arg(annotation);
            rec.approved     = true;
            rec.approver     = user;
            rec.approveTime  = rec.changeTime;
            m_changeLog.recordChange(rec);

            lock.unlock();
            emit alarmAnnotated(alarmId, annotation);
            emit changeRecorded(rec);
            LOG_INFO("AlarmEngine", QString("操作员注释: alarmId=%1, 注释=%2, 操作员=%3")
                         .arg(alarmId).arg(annotation).arg(user));
            return;
        }
    }
}

// ============================================================
// Level 2+4: 报警参数修改（含变更记录）
// ============================================================

bool AlarmEngine::setAlarmLimit(quint32 tagId, const QString& fieldName,
                                 float newValue, const QString& operatorName,
                                 const QString& reason)
{
    // 获取旧值
    QString oldValue;
    TagInfo tag = TagConfigMgr::instance().getTag(tagId);
    if (fieldName == "highHighLimit")    oldValue = QString::number(tag.highHighLimit, 'f', 1);
    else if (fieldName == "highLimit")   oldValue = QString::number(tag.highLimit, 'f', 1);
    else if (fieldName == "lowLimit")    oldValue = QString::number(tag.lowLimit, 'f', 1);
    else if (fieldName == "lowLowLimit") oldValue = QString::number(tag.lowLowLimit, 'f', 1);
    else if (fieldName == "deadband")    oldValue = QString::number(tag.deadband, 'f', 1);
    else if (fieldName == "onDelayMs")   oldValue = QString::number(tag.onDelayMs);
    else if (fieldName == "offDelayMs")  oldValue = QString::number(tag.offDelayMs);
    else return false;

    // 生成变更记录
    AlarmChangeRecord rec;
    rec.tagId        = tagId;
    rec.fieldName    = fieldName;
    rec.oldValue     = oldValue;
    rec.newValue     = QString::number(newValue, 'f', 1);
    rec.operatorName = operatorName;
    rec.reason       = reason;
    rec.changeTime   = QDateTime::currentMSecsSinceEpoch();

    // 写入变更日志（Level 4）
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
    rec.tagId        = tagId;
    rec.fieldName    = "priority";
    rec.oldValue     = oldVal;
    rec.newValue     = QString::number(static_cast<int>(newPriority));
    rec.operatorName = operatorName;
    rec.reason       = reason;
    rec.changeTime   = QDateTime::currentMSecsSinceEpoch();

    m_changeLog.recordChange(rec);
    emit alarmParameterChanged(tagId, "priority", rec.oldValue, rec.newValue);
    emit changeRecorded(rec);
    return true;
}

// ============================================================
// Level 2: 报警过滤与查询
// ============================================================

QList<AlarmEvent> AlarmEngine::activeAlarms() const
{
    QMutexLocker lock(&m_mutex);
    QList<AlarmEvent> result;
    for (const auto& event : m_activeAlarms) {
        if (!event.shelved && !event.isSuppressed()) result.append(event);
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

QList<AlarmEvent> AlarmEngine::filteredAlarms(const AlarmFilter& filter) const
{
    QMutexLocker lock(&m_mutex);
    QList<AlarmEvent> result;

    for (const auto& event : m_activeAlarms) {
        // 按优先级过滤
        if (!filter.priorities.isEmpty() &&
            !filter.priorities.contains(event.priority)) {
            continue;
        }

        // 按分类过滤
        if (!filter.classifications.isEmpty() &&
            !filter.classifications.contains(event.classification)) {
            continue;
        }

        // 按状态过滤
        if (!filter.states.isEmpty() &&
            !filter.states.contains(event.state)) {
            continue;
        }

        // 按区域过滤
        if (!filter.areas.isEmpty() &&
            !filter.areas.contains(event.area)) {
            continue;
        }

        // 按时间范围过滤
        if (filter.fromTime > 0 && event.triggerTime < filter.fromTime) {
            continue;
        }
        if (filter.toTime > 0 && event.triggerTime > filter.toTime) {
            continue;
        }

        // 按关键字搜索（位号名/描述）
        if (!filter.keyword.isEmpty()) {
            if (!event.tagName.contains(filter.keyword, Qt::CaseInsensitive) &&
                !event.description.contains(filter.keyword, Qt::CaseInsensitive)) {
                continue;
            }
        }

        // 屏蔽/抑制/停用过滤
        if (event.shelved && !filter.includeShelved) continue;
        if (event.isSuppressed() && !filter.includeSuppressed) continue;
        if (event.outOfService && !filter.includeOutOfService) continue;

        result.append(event);
    }

    return result;
}

int AlarmEngine::activeAlarmCount() const
{
    QMutexLocker lock(&m_mutex);
    int count = 0;
    for (const auto& event : m_activeAlarms) {
        if (!event.shelved && !event.isSuppressed()) count++;
    }
    return count;
}

int AlarmEngine::activeAlarmCount(AlarmLimit limit) const
{
    QMutexLocker lock(&m_mutex);
    int count = 0;
    for (const auto& event : m_activeAlarms) {
        if (!event.shelved && !event.isSuppressed() && event.limit == limit) count++;
    }
    return count;
}

int AlarmEngine::activeAlarmCount(AlarmPriority priority) const
{
    QMutexLocker lock(&m_mutex);
    int count = 0;
    for (const auto& event : m_activeAlarms) {
        if (!event.shelved && !event.isSuppressed() && event.priority == priority) count++;
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

int AlarmEngine::suppressedCount() const
{
    QMutexLocker lock(&m_mutex);
    int count = 0;
    for (const auto& event : m_activeAlarms) {
        if (event.state == AlarmState::SuppressedByDesign) count++;
    }
    return count;
}

int AlarmEngine::outOfServiceCount() const
{
    QMutexLocker lock(&m_mutex);
    int count = 0;
    for (const auto& event : m_activeAlarms) {
        if (event.state == AlarmState::OutOfService) count++;
    }
    return count;
}

QStringList AlarmEngine::areas() const
{
    QMutexLocker lock(&m_mutex);
    QStringList result;
    for (const auto& event : m_activeAlarms) {
        if (!event.area.isEmpty() && !result.contains(event.area)) {
            result.append(event.area);
        }
    }
    return result;
}

QList<AlarmEvent> AlarmEngine::alarmsByArea(const QString& area) const
{
    QMutexLocker lock(&m_mutex);
    QList<AlarmEvent> result;
    for (const auto& event : m_activeAlarms) {
        if (event.area == area) result.append(event);
    }
    return result;
}

QVector<AlarmFloodEvent> AlarmEngine::floodEvents() const
{
    QMutexLocker lock(&m_mutex);
    return m_floodEvents;
}

// ============================================================
// Level 3: 报警泛滥检测
// ============================================================

void AlarmEngine::checkFloodCondition()
{
    QMutexLocker lock(&m_mutex);

    AlarmKpiSnapshot kpi = m_kpiMonitor.snapshot(
        m_externalTotalActive, m_externalStaleCount, m_externalShelvedCount);

    int rate10min = kpi.alarmCount10min;

    if (!m_inFlood && rate10min > m_kpiMonitor.rateThreshold10min()) {
        // === 进入泛滥状态 ===
        m_inFlood = true;
        m_floodWindowStart = QDateTime::currentMSecsSinceEpoch();
        m_floodWindowCount = rate10min;

        // 泛滥期间自动抑制非 Critical 报警（减轻操作员负担）
        int autoSuppressedCount = 0;
        for (auto it = m_activeAlarms.begin(); it != m_activeAlarms.end(); ++it) {
            if (it->isActive() && it->priority < AlarmPriority::Critical) {
                // 保存原状态用于泛滥结束后恢复
                m_floodSuppressedAlarms.insert(it.key(), it->state);
                it->shelved = true;
                it->shelvedTime = QDateTime::currentMSecsSinceEpoch();
                it->shelveReason = QStringLiteral("报警泛滥自动抑制");
                it->shelveDurationSec = 300; // 5分钟
                it->shelveUser = QStringLiteral("System(FloodProtection)");
                it->state = AlarmState::Shelved;
                autoSuppressedCount++;
            }
        }

        AlarmFloodEvent floodEvent;
        floodEvent.startTime = m_floodWindowStart;
        floodEvent.endTime = 0;
        floodEvent.alarmCount = rate10min;
        floodEvent.peakRate = rate10min;

        // 计算贡献最多的位号（Top5）
        QMap<QString, int> tagCounts;
        for (const auto& ev : m_activeAlarms) {
            tagCounts[ev.tagName]++;
        }
        QList<QPair<int, QString>> sorted;
        for (auto it = tagCounts.begin(); it != tagCounts.end(); ++it) {
            sorted.append(qMakePair(it.value(), it.key()));
        }
        std::sort(sorted.begin(), sorted.end(), [](const auto& a, const auto& b) {
            return a.first > b.first;
        });
        for (int i = 0; i < qMin(5, sorted.size()); ++i) {
            floodEvent.topContributors.append(sorted[i].second);
        }

        m_floodEvents.append(floodEvent);

        lock.unlock();
        emit alarmFloodDetected(floodEvent);
        emit alarmCountChanged(activeAlarmCount(), unacknowledgedCount());
        LOG_WARN("AlarmEngine", QString("报警泛滥检测: 10分钟内%1个报警, 自动抑制%2个非Critical报警, Top: %3")
                     .arg(rate10min)
                     .arg(autoSuppressedCount)
                     .arg(floodEvent.topContributors.join(", ")));
        return;
    }

    if (m_inFlood) {
        // 更新当前泛滥事件
        if (!m_floodEvents.isEmpty()) {
            auto& currentFlood = m_floodEvents.last();
            currentFlood.alarmCount = qMax(currentFlood.alarmCount, rate10min);
            currentFlood.peakRate = qMax(currentFlood.peakRate, rate10min);
        }

        // 泛滥结束条件：10分钟报警率降到阈值一半以下
        if (rate10min <= m_kpiMonitor.rateThreshold10min() / 2) {
            m_inFlood = false;
            if (!m_floodEvents.isEmpty()) {
                auto& currentFlood = m_floodEvents.last();
                currentFlood.endTime = QDateTime::currentMSecsSinceEpoch();
            }

            // === 恢复泛滥期间被自动抑制的报警 ===
            int restoredCount = 0;
            for (auto it = m_floodSuppressedAlarms.begin();
                 it != m_floodSuppressedAlarms.end(); ++it) {
                auto alarmIt = m_activeAlarms.find(it.key());
                if (alarmIt != m_activeAlarms.end() && alarmIt->shelved
                    && alarmIt->shelveUser == QStringLiteral("System(FloodProtection)")) {
                    alarmIt->shelved = false;
                    alarmIt->shelvedTime = 0;
                    alarmIt->shelveReason.clear();
                    alarmIt->shelveUser.clear();
                    alarmIt->state = it.value(); // 恢复到泛滥前的状态
                    restoredCount++;
                }
            }
            m_floodSuppressedAlarms.clear();

            lock.unlock();
            emit alarmCountChanged(activeAlarmCount(), unacknowledgedCount());
            LOG_INFO("AlarmEngine", QString("报警泛滥结束, 恢复%1个报警").arg(restoredCount));
            return;
        }
    }
}

// ============================================================
// Level 1: 重复报警保护（Chattering Protection）
// ============================================================

bool AlarmEngine::checkChattering(quint32 tagId)
{
    // 注意：此方法在 m_mutex 已锁定的上下文中调用

    TagInfo tagInfo = TagConfigMgr::instance().getTag(tagId);
    int maxRepeats = tagInfo.maxRepeatsPerMin;
    if (maxRepeats <= 0) maxRepeats = 3; // 默认值

    auto& state = m_chatteringState[tagId];
    qint64 now = QDateTime::currentMSecsSinceEpoch();

    // 1分钟窗口重置
    if (state.windowStart == 0 || (now - state.windowStart) > 60000) {
        state.count = 1;
        state.windowStart = now;
        state.autoShelved = false;
        return false;
    }

    state.count++;

    // 超过阈值，自动屏蔽
    if (state.count > maxRepeats && !state.autoShelved) {
        state.autoShelved = true;

        // 发出 Chattering 检测信号（需要先解锁 mutex）
        // 注意：这里不能直接调用 shelveAlarm（会死锁），由外部处理
        // 标记此 tag 为自动屏蔽
        auto activeIt = m_activeAlarms.find(tagId);
        if (activeIt != m_activeAlarms.end()) {
            activeIt->shelved = true;
            activeIt->shelvedTime = now;
            activeIt->shelveReason = QString("Chattering自动屏蔽(%1次/分钟)").arg(state.count);
            activeIt->shelveDurationSec = 3600; // 默认屏蔽1小时
            activeIt->shelveUser = "System";
            activeIt->state = AlarmState::Shelved;
            m_shelveDeadlines[tagId] = now + 3600 * 1000;
        }

        LOG_WARN("AlarmEngine", QString("Chattering保护: tagId=%1, %2次/分钟, 自动屏蔽1小时")
                     .arg(tagId).arg(state.count));

        // 延迟发送信号（避免在 mutex 内发送）
        QMetaObject::invokeMethod(this, [this, tagId, count = state.count]() {
            emit chatteringAlarmDetected(tagId, count);
            emit alarmShelved(tagId, QString("Chattering自动屏蔽(%1次/分钟)").arg(count), 3600);
            emit alarmCountChanged(activeAlarmCount(), unacknowledgedCount());
        }, Qt::QueuedConnection);

        return true;
    }

    return false;
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
// 内部辅助方法
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
    case AlarmLimit::HighHigh:     return "高高报";
    case AlarmLimit::High:         return "高报";
    case AlarmLimit::Low:          return "低报";
    case AlarmLimit::LowLow:       return "低低报";
    case AlarmLimit::Deviation:    return "偏差报警";
    case AlarmLimit::RateOfChange: return "变化率报警";
    default:                       return "未知";
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

// ============================================================
// 便利方法实现（UI层简化调用）
// ============================================================

/**
 * @brief 便利方法：屏蔽报警（分钟为单位）
 *
 * UI层直接传入分钟数，自动转换为秒并获取当前登录用户。
 */
void AlarmEngine::shelveAlarm(quint32 tagId, int durationMin)
{
    QString user = AuthManager::instance().currentUsername();
    QString reason = QStringLiteral("操作员屏蔽 %1分钟").arg(durationMin);
    shelveAlarm(tagId, reason, durationMin * 60, user);
}

/**
 * @brief 便利方法：设计抑制（自动获取当前用户）
 */
void AlarmEngine::suppressAlarm(quint32 tagId, const QString& reason)
{
    QString user = AuthManager::instance().currentUsername();
    suppressByDesign(tagId, reason, user, user);
}

/**
 * @brief 便利方法：取消设计抑制
 */
void AlarmEngine::unsuppressAlarm(quint32 tagId)
{
    unsuppressByDesign(tagId);
}

/**
 * @brief 便利方法：设备停用（自动获取当前用户）
 */
void AlarmEngine::setOutOfService(quint32 tagId, const QString& reason)
{
    QString user = AuthManager::instance().currentUsername();
    setOutOfService(tagId, reason, user, QString());
}

/**
 * @brief 便利方法：添加操作员注释（自动获取当前用户）
 */
void AlarmEngine::annotateAlarm(const QString& alarmId, const QString& annotation)
{
    QString user = AuthManager::instance().currentUsername();
    annotateAlarm(alarmId, annotation, user);
}

/**
 * @brief 便利方法：获取当前 KPI 快照
 *
 * 自动计算外部统计值并传入 KpiMonitor。
 */
AlarmKpiSnapshot AlarmEngine::kpiSnapshot() const
{
    QMutexLocker lock(&m_mutex);

    // 计算当前统计值
    int totalActive = m_activeAlarms.size();
    int staleCount = 0;
    int shelvedCount = 0;
    int suppressedCount = 0;
    int criticalCount = 0;
    int majorCount = 0;
    int minorCount = 0;
    int advisoryCount = 0;
    qint64 now = QDateTime::currentMSecsSinceEpoch();

    for (const auto& event : m_activeAlarms) {
        // 陈旧报警：未确认超过30分钟
        if (!event.acknowledged && event.triggerTime > 0) {
            qint64 elapsed = (now - event.triggerTime) / 1000;
            if (elapsed > 1800) {
                staleCount++;
            }
        }

        // 屏蔽计数
        if (event.isShelved()) shelvedCount++;

        // 抑制计数
        if (event.isSuppressed()) suppressedCount++;

        // 优先级分布
        switch (event.priority) {
        case AlarmPriority::Critical: criticalCount++; break;
        case AlarmPriority::Major:    majorCount++; break;
        case AlarmPriority::Minor:    minorCount++; break;
        case AlarmPriority::Advisory: advisoryCount++; break;
        }
    }

    // 获取基础 KPI 快照
    auto snapshot = m_kpiMonitor.snapshot(totalActive, staleCount, shelvedCount);

    // 补充 AlarmEngine 层面的统计
    snapshot.criticalCount = criticalCount;
    snapshot.majorCount = majorCount;
    snapshot.minorCount = minorCount;
    snapshot.advisoryCount = advisoryCount;
    snapshot.shelvedCount = shelvedCount;
    snapshot.suppressedCount = suppressedCount;
    snapshot.floodEventCount = m_floodEvents.size();
    snapshot.chatteringCount = 0;
    for (const auto& cs : m_chatteringState) {
        if (cs.autoShelved) snapshot.chatteringCount++;
    }

    // 计算系统健康评分
    float score = 100.0f;

    // 10分钟报警率扣分（EEMUA 191: ≤10为可管理）
    if (snapshot.alarmCount10min > 10) {
        score -= qMin(30.0f, (snapshot.alarmCount10min - 10) * 1.5f);
    }

    // 陈旧报警扣分
    if (snapshot.staleAlarmPercent > 5) {
        score -= qMin(20.0f, (snapshot.staleAlarmPercent - 5) * 2.0f);
    }

    // 报警洪峰扣分
    score -= qMin(20.0f, snapshot.floodEventCount * 5.0f);

    // 颤振报警扣分
    score -= qMin(15.0f, snapshot.chatteringCount * 3.0f);

    // 屏蔽/抑制过多扣分
    int suppressedTotal = shelvedCount + suppressedCount;
    if (suppressedTotal > totalActive * 0.1 && totalActive > 0) {
        score -= qMin(15.0f, (suppressedTotal - totalActive * 0.1) * 2.0f);
    }

    snapshot.systemHealthScore = qBound(0.0f, score, 100.0f);

    // 健康等级
    if (snapshot.systemHealthScore >= 80) snapshot.healthGrade = "A";
    else if (snapshot.systemHealthScore >= 60) snapshot.healthGrade = "B";
    else if (snapshot.systemHealthScore >= 40) snapshot.healthGrade = "C";
    else if (snapshot.systemHealthScore >= 20) snapshot.healthGrade = "D";
    else snapshot.healthGrade = "F";

    return snapshot;
}

/**
 * @brief 便利方法：获取 Top-N 频发报警
 *
 * 从活跃报警和历史报警中统计触发次数最多的位号。
 * 用于 Bad Actor 分析（ISA-18.2 要求识别频发报警）。
 */
QVector<QPair<quint32, int>> AlarmEngine::topFrequentAlarms(int topN) const
{
    QMutexLocker lock(&m_mutex);

    // 统计每位号的触发次数
    QMap<quint32, int> tagCounts;

    for (const auto& event : m_activeAlarms) {
        tagCounts[event.tagId] += event.repeatCount + 1;
    }

    for (const auto& event : m_alarmHistory) {
        tagCounts[event.tagId] += event.repeatCount + 1;
    }

    // 转换为列表并排序
    QVector<QPair<quint32, int>> sorted;
    for (auto it = tagCounts.begin(); it != tagCounts.end(); ++it) {
        sorted.append({it.key(), it.value()});
    }

    std::sort(sorted.begin(), sorted.end(),
              [](const auto& a, const auto& b) { return a.second > b.second; });

    // 取 Top-N
    if (sorted.size() > topN) {
        sorted.resize(topN);
    }

    return sorted;
}

// ============================================================
// Level 2: Suppression-by-Condition 条件抑制引擎（不足1修复）
// ============================================================

bool AlarmEngine::addSuppressionRule(const SuppressionRule& rule)
{
    QMutexLocker lock(&m_suppressionMutex);

    // 验证：目标位号 - 条件位号不能相同
    if (rule.targetTagId == 0 || rule.conditionTagId == 0) {
        LOG_WARN("AlarmEngine", "添加抑制规则失败: 位号ID无效");
        return false;
    }

    // 生成规则ID
    SuppressionRule newRule = rule;
    if (newRule.ruleId == 0) {
        newRule.ruleId = static_cast<quint32>(m_suppressionRules.size()) + 1;
    }
    if (newRule.createdTime == 0) {
        newRule.createdTime = QDateTime::currentMSecsSinceEpoch();
    }

    m_suppressionRules.append(newRule);
    LOG_INFO("AlarmEngine", QString("添加抑制规则: ruleId=%1, 目标=%2, 条件=%3%4, 原因=%5")
                 .arg(newRule.ruleId)
                 .arg(newRule.targetTagId)
                 .arg(newRule.conditionTagId)
                 .arg(newRule.conditionExpr)
                 .arg(newRule.reason));
    return true;
}

void AlarmEngine::removeSuppressionRule(quint32 ruleId)
{
    QMutexLocker lock(&m_suppressionMutex);
    for (int i = 0; i < m_suppressionRules.size(); ++i) {
        if (m_suppressionRules[i].ruleId == ruleId) {
            m_suppressionRules.removeAt(i);
            LOG_INFO("AlarmEngine", QString("移除抑制规则: ruleId=%1").arg(ruleId));
            return;
        }
    }
}

void AlarmEngine::setSuppressionRuleEnabled(quint32 ruleId, bool enabled)
{
    QMutexLocker lock(&m_suppressionMutex);
    for (auto& rule : m_suppressionRules) {
        if (rule.ruleId == ruleId) {
            rule.enabled = enabled;
            LOG_INFO("AlarmEngine", QString("抑制规则 %1: ruleId=%2").arg(enabled ? "启用" : "禁用").arg(ruleId));
            return;
        }
    }
}

QVector<SuppressionRule> AlarmEngine::suppressionRules() const
{
    QMutexLocker lock(&m_suppressionMutex);
    return m_suppressionRules;
}

bool AlarmEngine::evaluateSuppression(quint32 tagId) const
{
    QMutexLocker lock(&m_suppressionMutex);

    for (const auto& rule : m_suppressionRules) {
        if (!rule.enabled) continue;
        if (rule.targetTagId != tagId) continue;

        // 检查条件位号的当前值
        if (!m_doubleBuffer) {
            LOG_DEBUG("AlarmEngine", "DoubleBuffer 未设置，跳过条件抑制评估");
            continue;
        }

        auto snap = m_doubleBuffer->readTag(rule.conditionTagId);
        if (snap.tagId == 0) continue; // 条件位号不存在

        float condValue = snap.currentValue;

        // 解析条件表达式
        bool conditionMet = false;
        if (rule.conditionExpr == "==0") {
            conditionMet = (qAbs(condValue) < 0.001f);
        } else if (rule.conditionExpr == "==1") {
            conditionMet = (qAbs(condValue - 1.0f) < 0.001f);
        } else if (rule.conditionExpr == ">0") {
            conditionMet = (condValue > 0.0f);
        } else if (rule.conditionExpr.startsWith(">")) {
            float threshold = rule.conditionExpr.mid(1).toFloat();
            conditionMet = (condValue > threshold);
        } else if (rule.conditionExpr.startsWith("<")) {
            float threshold = rule.conditionExpr.mid(1).toFloat();
            conditionMet = (condValue < threshold);
        } else {
            LOG_WARN("AlarmEngine", QString("无法解析条件表达式: %1").arg(rule.conditionExpr));
            continue;
        }

        if (conditionMet) {
            LOG_DEBUG("AlarmEngine", QString("报警被条件抑制: tagId=%1, 规则=%2, 条件值=%3")
                          .arg(tagId).arg(rule.ruleId).arg(condValue));
            return true;
        }
    }
    return false;
}
