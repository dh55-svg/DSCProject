#include "AlarmSqliteRepo.h"
#include <QSqlQuery>
#include <QSqlError>
#include <QDateTime>
#include <QDebug>

namespace {

AlarmEvent deserializeAlarmEvent(const QSqlQuery& q) {
    AlarmEvent e;
    e.alarmId              = q.value("alarm_id").toString();
    e.tagId                = q.value("tag_id").toUInt();
    e.tagName              = q.value("tag_name").toString();
    e.limit                = static_cast<AlarmLimit>(q.value("alarm_limit").toInt());
    e.priority             = static_cast<AlarmPriority>(q.value("priority").toInt());
    e.classification       = static_cast<AlarmClassification>(q.value("classification").toInt());
    e.description          = q.value("description").toString();
    e.triggerValue         = q.value("trigger_value").toFloat();
    e.thresholdValue       = q.value("threshold_value").toFloat();
    e.triggerTime          = q.value("trigger_time").toLongLong();
    e.state                = static_cast<AlarmState>(q.value("state").toInt());
    e.acknowledged         = q.value("acknowledged").toBool();
    e.acknowledgeTime      = q.value("ack_time").toLongLong();
    e.acknowledgeUser      = q.value("ack_user").toString();
    e.active               = q.value("active").toBool();
    e.returnToNormalTime   = q.value("rtn_time").toLongLong();
    e.returnAckTime        = q.value("rtn_ack_time").toLongLong();
    e.returnValue          = q.value("return_value").toFloat();
    e.shelved              = q.value("shelved").toBool();
    e.shelvedTime          = q.value("shelved_time").toLongLong();
    e.shelveReason         = q.value("shelve_reason").toString();
    e.shelveDurationSec    = q.value("shelve_duration").toInt();
    e.shelveUser           = q.value("shelve_user").toString();
    e.suppressionType      = static_cast<AlarmSuppressionType>(q.value("suppression_type").toInt());
    e.suppressionReason    = q.value("suppression_reason").toString();
    e.suppressionUser      = q.value("suppression_user").toString();
    e.suppressionTime      = q.value("suppression_time").toLongLong();
    e.outOfService         = q.value("out_of_service").toBool();
    e.outOfServiceReason   = q.value("oos_reason").toString();
    e.outOfServiceUser     = q.value("oos_user").toString();
    e.workOrderNo          = q.value("work_order").toString();
    e.operatorAnnotation   = q.value("annotation").toString();
    e.annotationTime       = q.value("annotation_time").toLongLong();
    e.annotationUser       = q.value("annotation_user").toString();
    e.area                 = q.value("area").toString();
    e.zone                 = q.value("zone").toString();
    e.notificationType     = static_cast<AlarmNotificationType>(q.value("notification_type").toInt());
    e.notificationCount    = q.value("notification_count").toInt();
    e.repeatCount          = q.value("repeat_count").toInt();
    e.firstTriggerTime     = q.value("first_trigger_time").toLongLong();
    return e;
}

AlarmChangeRecord deserializeChangeRecord(const QSqlQuery& q) {
    AlarmChangeRecord r;
    r.changeTime   = q.value("change_time").toLongLong();
    r.operatorName = q.value("operator_name").toString();
    r.tagId        = q.value("tag_id").toUInt();
    r.fieldName    = q.value("field_name").toString();
    r.oldValue     = q.value("old_value").toString();
    r.newValue     = q.value("new_value").toString();
    r.reason       = q.value("reason").toString();
    r.approved     = q.value("approved").toBool();
    r.approver     = q.value("approver").toString();
    r.approveTime  = q.value("approve_time").toLongLong();
    r.rejected     = q.value("rejected").toBool();
    r.rejectReason = q.value("reject_reason").toString();
    r.workOrderNo  = q.value("work_order").toString();
    r.validUntil   = q.value("valid_until").toLongLong();
    r.autoReverted = q.value("auto_reverted").toBool();
    r.sessionId    = q.value("session_id").toString();
    r.workstation  = q.value("workstation").toString();
    return r;
}

AlarmKpiSnapshot deserializeKpiSnapshot(const QSqlQuery& q) {
    AlarmKpiSnapshot s;
    s.timestamp         = q.value("timestamp").toLongLong();
    s.alarmCount10min   = q.value("alarm_count_10min").toInt();
    s.avgPerHour        = q.value("avg_per_hour").toFloat();
    s.peakCount10min    = q.value("peak_count_10min").toInt();
    s.staleCount        = q.value("stale_count").toInt();
    s.totalActive        = q.value("total_active").toInt();
    s.shelvedCount      = q.value("shelved_count").toInt();
    s.suppressedCount   = q.value("suppressed_count").toInt();
    s.floodEventCount   = q.value("flood_event_count").toInt();
    s.floodDurationMin  = q.value("flood_duration_min").toFloat();
    s.avgAckTimeSec     = q.value("avg_ack_time_sec").toFloat();
    s.chatteringCount   = q.value("chattering_count").toInt();
    s.staleAlarmPercent = q.value("stale_alarm_percent").toInt();
    s.criticalCount     = q.value("critical_count").toInt();
    s.majorCount        = q.value("major_count").toInt();
    s.minorCount        = q.value("minor_count").toInt();
    s.advisoryCount     = q.value("advisory_count").toInt();
    s.systemHealthScore = q.value("health_score").toFloat();
    s.healthGrade       = q.value("health_grade").toString();
    s.top5Frequent      = q.value("top5_frequent").toString().split(',', Qt::SkipEmptyParts);
    s.top5Stale         = q.value("top5_stale").toString().split(',', Qt::SkipEmptyParts);
    return s;
}

} // anonymous namespace

AlarmSqliteRepo::AlarmSqliteRepo(std::shared_ptr<ConnectionPool> pool) : m_pool(pool) {
    ensureTables();
}

void AlarmSqliteRepo::ensureTables() {
    auto db = m_pool->acquire();
    QSqlQuery q(db);
    q.exec("CREATE TABLE IF NOT EXISTS alarm_events ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT, alarm_id TEXT UNIQUE, tag_id INTEGER, tag_name TEXT, "
        "alarm_limit INTEGER, priority INTEGER, classification INTEGER, description TEXT, trigger_value REAL, "
        "threshold_value REAL, trigger_time INTEGER, state INTEGER, acknowledged INTEGER, ack_time INTEGER, "
        "ack_user TEXT, active INTEGER, rtn_time INTEGER, rtn_ack_time INTEGER, return_value REAL, "
        "shelved INTEGER, shelved_time INTEGER, shelve_reason TEXT, shelve_duration INTEGER, shelve_user TEXT, "
        "suppression_type INTEGER, suppression_reason TEXT, suppression_user TEXT, suppression_time INTEGER, "
        "out_of_service INTEGER, oos_reason TEXT, oos_user TEXT, work_order TEXT, "
        "annotation TEXT, annotation_time INTEGER, annotation_user TEXT, "
        "area TEXT, zone TEXT, notification_type INTEGER, notification_count INTEGER, repeat_count INTEGER, "
        "first_trigger_time INTEGER)");

    q.exec("CREATE TABLE IF NOT EXISTS alarm_change_log ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT, change_time INTEGER, operator_name TEXT, "
        "tag_id INTEGER, field_name TEXT, old_value TEXT, new_value TEXT, reason TEXT, "
        "approved INTEGER, approver TEXT, approve_time INTEGER, rejected INTEGER, reject_reason TEXT, "
        "work_order TEXT, valid_until INTEGER, auto_reverted INTEGER, session_id TEXT, workstation TEXT)");

    q.exec("CREATE TABLE IF NOT EXISTS alarm_kpi_snapshots ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT, timestamp INTEGER, alarm_count_10min INTEGER, avg_per_hour REAL, "
        "peak_count_10min INTEGER, stale_count INTEGER, total_active INTEGER, shelved_count INTEGER, suppressed_count INTEGER, "
        "flood_event_count INTEGER, flood_duration_min REAL, avg_ack_time_sec REAL, chattering_count INTEGER, "
        "stale_alarm_percent INTEGER, critical_count INTEGER, major_count INTEGER, minor_count INTEGER, advisory_count INTEGER, "
        "health_score REAL, health_grade TEXT, top5_frequent TEXT, top5_stale TEXT)");

    m_pool->release(db);
}

void AlarmSqliteRepo::insertEvent(const AlarmEvent& e) {
    batchInsertEvents({e});
}

void AlarmSqliteRepo::batchInsertEvents(const QVector<AlarmEvent>& events) {
    if (events.isEmpty()) return;
    auto db = m_pool->acquire();
    QSqlQuery q(db);
    q.prepare("INSERT OR REPLACE INTO alarm_events (alarm_id, tag_id, tag_name, alarm_limit, priority, classification, "
        "description, trigger_value, threshold_value, trigger_time, state, active, area, zone, "
        "suppression_type, notification_type, first_trigger_time) "
        "VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)");
    for (const auto& e : events) {
        q.addBindValue(e.alarmId);
        q.addBindValue(e.tagId);
        q.addBindValue(e.tagName);
        q.addBindValue(static_cast<int>(e.limit));
        q.addBindValue(static_cast<int>(e.priority));
        q.addBindValue(static_cast<int>(e.classification));
        q.addBindValue(e.description);
        q.addBindValue(e.triggerValue);
        q.addBindValue(e.thresholdValue);
        q.addBindValue(e.triggerTime);
        q.addBindValue(static_cast<int>(e.state));
        q.addBindValue(e.active ? 1 : 0);
        q.addBindValue(e.area);
        q.addBindValue(e.zone);
        q.addBindValue(static_cast<int>(e.suppressionType));
        q.addBindValue(static_cast<int>(e.notificationType));
        q.addBindValue(e.firstTriggerTime > 0 ? e.firstTriggerTime : e.triggerTime);
        if (!q.exec()) qWarning() << "AlarmSqliteRepo: insertEvent failed:" << q.lastError().text();
    }
    m_pool->release(db);
}

void AlarmSqliteRepo::updateAck(const QString& alarmId, const QString& user, qint64 ts) {
    auto db = m_pool->acquire();
    QSqlQuery q(db);
    q.prepare("UPDATE alarm_events SET acknowledged=1, ack_user=?, ack_time=?, state=? WHERE alarm_id=?");
    q.addBindValue(user);
    q.addBindValue(ts);
    q.addBindValue(static_cast<int>(AlarmState::ActiveAcknowledged));
    q.addBindValue(alarmId);
    if (!q.exec()) qWarning() << "AlarmSqliteRepo: updateAck failed:" << q.lastError().text();
    m_pool->release(db);
}

void AlarmSqliteRepo::updateEvent(const QString& alarmId, const QString& field, const QString& value, qint64 ts) {
    auto db = m_pool->acquire();
    QSqlQuery q(db);
    q.prepare(QString("UPDATE alarm_events SET %1=?, trigger_time=? WHERE alarm_id=?").arg(field));
    q.addBindValue(value);
    q.addBindValue(ts);
    q.addBindValue(alarmId);
    if (!q.exec()) qWarning() << "AlarmSqliteRepo: updateEvent failed:" << q.lastError().text();
    m_pool->release(db);
}

QVector<AlarmEvent> AlarmSqliteRepo::queryActive() {
    auto db = m_pool->acquire();
    QSqlQuery q(db);
    q.exec("SELECT * FROM alarm_events WHERE active=1 AND shelved=0 AND suppression_type=0 AND out_of_service=0");
    QVector<AlarmEvent> results;
    while (q.next()) results.append(deserializeAlarmEvent(q));
    m_pool->release(db);
    return results;
}

QVector<AlarmEvent> AlarmSqliteRepo::queryEvents(const AlarmFilter& filter, int limit) {
    auto db = m_pool->acquire();
    QString sql = "SELECT * FROM alarm_events WHERE 1=1";

    if (!filter.priorities.isEmpty()) {
        QStringList nums;
        for (auto p : filter.priorities) nums << QString::number(static_cast<int>(p));
        sql += QString(" AND priority IN (%1)").arg(nums.join(','));
    }
    if (!filter.classifications.isEmpty()) {
        QStringList nums;
        for (auto c : filter.classifications) nums << QString::number(static_cast<int>(c));
        sql += QString(" AND classification IN (%1)").arg(nums.join(','));
    }
    if (!filter.states.isEmpty()) {
        QStringList nums;
        for (auto s : filter.states) nums << QString::number(static_cast<int>(s));
        sql += QString(" AND state IN (%1)").arg(nums.join(','));
    }
    if (!filter.areas.isEmpty()) {
        QStringList areaList;
        for (const auto& a : filter.areas) areaList << QString("'%1'").arg(a);
        sql += QString(" AND area IN (%1)").arg(areaList.join(','));
    }
    if (filter.fromTime > 0)
        sql += QString(" AND trigger_time >= %1").arg(filter.fromTime);
    if (filter.toTime > 0)
        sql += QString(" AND trigger_time <= %1").arg(filter.toTime);
    if (!filter.keyword.isEmpty())
        sql += QString(" AND (tag_name LIKE '%%1%' OR description LIKE '%%1%' OR alarm_id LIKE '%%1%')").arg(filter.keyword);
    if (!filter.includeShelved) sql += " AND shelved=0";
    if (!filter.includeSuppressed) sql += " AND suppression_type=0";
    if (!filter.includeOutOfService) sql += " AND out_of_service=0";

    sql += " ORDER BY trigger_time DESC";
    if (limit > 0) sql += QString(" LIMIT %1").arg(limit);

    QSqlQuery q(db);
    q.exec(sql);
    QVector<AlarmEvent> results;
    while (q.next()) results.append(deserializeAlarmEvent(q));
    m_pool->release(db);
    return results;
}

QVector<AlarmEvent> AlarmSqliteRepo::queryHistory(qint64 start, qint64 end, int limit) {
    auto db = m_pool->acquire();
    QSqlQuery q(db);
    q.prepare("SELECT * FROM alarm_events WHERE trigger_time >= ? AND trigger_time <= ? ORDER BY trigger_time DESC LIMIT ?");
    q.addBindValue(start);
    q.addBindValue(end);
    q.addBindValue(limit > 0 ? limit : 1000);
    q.exec();
    QVector<AlarmEvent> results;
    while (q.next()) results.append(deserializeAlarmEvent(q));
    m_pool->release(db);
    return results;
}

void AlarmSqliteRepo::insertChangeRecord(const AlarmChangeRecord& r) {
    auto db = m_pool->acquire();
    QSqlQuery q(db);
    q.prepare("INSERT INTO alarm_change_log (change_time, operator_name, tag_id, field_name, old_value, new_value, reason) "
        "VALUES (?,?,?,?,?,?,?)");
    q.addBindValue(r.changeTime);
    q.addBindValue(r.operatorName);
    q.addBindValue(r.tagId);
    q.addBindValue(r.fieldName);
    q.addBindValue(r.oldValue);
    q.addBindValue(r.newValue);
    q.addBindValue(r.reason);
    if (!q.exec()) qWarning() << "AlarmSqliteRepo: insertChangeRecord failed:" << q.lastError().text();
    m_pool->release(db);
}

QVector<AlarmChangeRecord> AlarmSqliteRepo::queryChangeRecords(quint32 tagId, int limit) {
    auto db = m_pool->acquire();
    QSqlQuery q(db);
    q.prepare("SELECT * FROM alarm_change_log WHERE tag_id=? ORDER BY change_time DESC LIMIT ?");
    q.addBindValue(tagId);
    q.addBindValue(limit > 0 ? limit : 500);
    q.exec();
    QVector<AlarmChangeRecord> results;
    while (q.next()) results.append(deserializeChangeRecord(q));
    m_pool->release(db);
    return results;
}

QVector<AlarmChangeRecord> AlarmSqliteRepo::queryPendingApprovals() {
    auto db = m_pool->acquire();
    QSqlQuery q(db);
    q.exec("SELECT * FROM alarm_change_log WHERE approved=0 AND rejected=0 ORDER BY change_time DESC LIMIT 200");
    QVector<AlarmChangeRecord> results;
    while (q.next()) results.append(deserializeChangeRecord(q));
    m_pool->release(db);
    return results;
}

void AlarmSqliteRepo::updateChangeApproval(int recordId, bool approved, const QString& approver, const QString& rejectReason) {
    auto db = m_pool->acquire();
    QSqlQuery q(db);
    q.prepare("UPDATE alarm_change_log SET approved=?, approver=?, approve_time=?, rejected=?, reject_reason=? WHERE id=?");
    q.addBindValue(approved ? 1 : 0);
    q.addBindValue(approver);
    q.addBindValue(QDateTime::currentMSecsSinceEpoch());
    q.addBindValue(approved ? 0 : 1);
    q.addBindValue(rejectReason);
    q.addBindValue(recordId);
    if (!q.exec()) qWarning() << "AlarmSqliteRepo: updateChangeApproval failed:" << q.lastError().text();
    m_pool->release(db);
}

void AlarmSqliteRepo::insertKpiSnapshot(const AlarmKpiSnapshot& s) {
    auto db = m_pool->acquire();
    QSqlQuery q(db);
    q.prepare("INSERT INTO alarm_kpi_snapshots (timestamp, alarm_count_10min, avg_per_hour, peak_count_10min, "
        "stale_count, total_active, shelved_count, suppressed_count, flood_event_count, flood_duration_min, "
        "avg_ack_time_sec, chattering_count, stale_alarm_percent, critical_count, major_count, minor_count, "
        "advisory_count, health_score, health_grade, top5_frequent, top5_stale) "
        "VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)");
    q.addBindValue(s.timestamp);
    q.addBindValue(s.alarmCount10min);
    q.addBindValue(s.avgPerHour);
    q.addBindValue(s.peakCount10min);
    q.addBindValue(s.staleCount);
    q.addBindValue(s.totalActive);
    q.addBindValue(s.shelvedCount);
    q.addBindValue(s.suppressedCount);
    q.addBindValue(s.floodEventCount);
    q.addBindValue(s.floodDurationMin);
    q.addBindValue(s.avgAckTimeSec);
    q.addBindValue(s.chatteringCount);
    q.addBindValue(s.staleAlarmPercent);
    q.addBindValue(s.criticalCount);
    q.addBindValue(s.majorCount);
    q.addBindValue(s.minorCount);
    q.addBindValue(s.advisoryCount);
    q.addBindValue(s.systemHealthScore);
    q.addBindValue(s.healthGrade);
    q.addBindValue(s.top5Frequent.join(','));
    q.addBindValue(s.top5Stale.join(','));
    if (!q.exec()) qWarning() << "AlarmSqliteRepo: insertKpiSnapshot failed:" << q.lastError().text();
    m_pool->release(db);
}

QVector<AlarmKpiSnapshot> AlarmSqliteRepo::queryKpiHistory(qint64 start, qint64 end, int limit) {
    auto db = m_pool->acquire();
    QSqlQuery q(db);
    q.prepare("SELECT * FROM alarm_kpi_snapshots WHERE timestamp >= ? AND timestamp <= ? ORDER BY timestamp DESC LIMIT ?");
    q.addBindValue(start);
    q.addBindValue(end);
    q.addBindValue(limit > 0 ? limit : 144);
    q.exec();
    QVector<AlarmKpiSnapshot> results;
    while (q.next()) results.append(deserializeKpiSnapshot(q));
    m_pool->release(db);
    return results;
}

void AlarmSqliteRepo::purgeOldRecords(int keepDays) {
    auto db = m_pool->acquire();
    qint64 now = QDateTime::currentMSecsSinceEpoch();
    qint64 alarmCutoff = now - static_cast<qint64>(365) * 86400000LL;
    qint64 changeCutoff = now - static_cast<qint64>(730) * 86400000LL;
    qint64 kpiCutoff = now - static_cast<qint64>(90) * 86400000LL;
    Q_UNUSED(keepDays);

    QSqlQuery q(db);
    q.prepare("DELETE FROM alarm_events WHERE trigger_time < ?");
    q.addBindValue(alarmCutoff); q.exec();
    q.prepare("DELETE FROM alarm_change_log WHERE change_time < ?");
    q.addBindValue(changeCutoff); q.exec();
    q.prepare("DELETE FROM alarm_kpi_snapshots WHERE timestamp < ?");
    q.addBindValue(kpiCutoff); q.exec();
    m_pool->release(db);
}
