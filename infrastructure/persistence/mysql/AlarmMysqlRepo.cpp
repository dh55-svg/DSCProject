#include "AlarmMysqlRepo.h"
#include <QSqlQuery>
#include <QSqlError>
#include <QDebug>

AlarmMysqlRepo::AlarmMysqlRepo(std::shared_ptr<ConnectionPool> pool) : m_pool(pool) {
    ensureTables();
}

void AlarmMysqlRepo::ensureTables() {
    auto db = m_pool->acquire();
    QSqlQuery q(db);
    q.exec("CREATE TABLE IF NOT EXISTS alarm_events ("
        "id BIGINT AUTO_INCREMENT PRIMARY KEY, alarm_id VARCHAR(64) UNIQUE, tag_id INT, tag_name VARCHAR(128), "
        "alarm_limit INT, priority INT, classification INT, description TEXT, trigger_value DOUBLE, "
        "threshold_value DOUBLE, trigger_time BIGINT, state INT, acknowledged INT, ack_time BIGINT, "
        "ack_user VARCHAR(64), active INT, rtn_time BIGINT, rtn_ack_time BIGINT, return_value DOUBLE, "
        "shelved INT, shelved_time BIGINT, shelve_reason TEXT, shelve_duration INT, shelve_user VARCHAR(64), "
        "suppression_type INT, suppression_reason TEXT, suppression_user VARCHAR(64), suppression_time BIGINT, "
        "out_of_service INT, oos_reason TEXT, oos_user VARCHAR(64), work_order VARCHAR(64), "
        "annotation TEXT, annotation_time BIGINT, annotation_user VARCHAR(64), "
        "area VARCHAR(64), zone VARCHAR(64), notification_type INT, notification_count INT, repeat_count INT, "
        "first_trigger_time BIGINT)");

    q.exec("CREATE TABLE IF NOT EXISTS alarm_change_log ("
        "id BIGINT AUTO_INCREMENT PRIMARY KEY, change_time BIGINT, operator_name VARCHAR(64), "
        "tag_id INT, field_name VARCHAR(64), old_value TEXT, new_value TEXT, reason TEXT, "
        "approved INT, approver VARCHAR(64), approve_time BIGINT, rejected INT, reject_reason TEXT, "
        "work_order VARCHAR(64), valid_until BIGINT, auto_reverted INT, session_id VARCHAR(64), workstation VARCHAR(64))");

    q.exec("CREATE TABLE IF NOT EXISTS alarm_kpi_snapshots ("
        "id BIGINT AUTO_INCREMENT PRIMARY KEY, timestamp BIGINT, alarm_count_10min INT, avg_per_hour DOUBLE, "
        "peak_count_10min INT, stale_count INT, total_active INT, shelved_count INT, suppressed_count INT, "
        "flood_event_count INT, flood_duration_min DOUBLE, avg_ack_time_sec DOUBLE, chattering_count INT, "
        "stale_alarm_percent INT, critical_count INT, major_count INT, minor_count INT, advisory_count INT, "
        "health_score DOUBLE, health_grade VARCHAR(4), top5_frequent TEXT, top5_stale TEXT)");

    m_pool->release(db);
}

void AlarmMysqlRepo::insertEvent(const AlarmEvent& e) {
    batchInsertEvents({e});
}

void AlarmMysqlRepo::batchInsertEvents(const QVector<AlarmEvent>& events) {
    if (events.isEmpty()) return;
    auto db = m_pool->acquire();
    QSqlQuery q(db);
    q.prepare("INSERT INTO alarm_events (alarm_id, tag_id, tag_name, alarm_limit, priority, classification, "
        "description, trigger_value, threshold_value, trigger_time, state) "
        "VALUES (?,?,?,?,?,?,?,?,?,?,?)");
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
        if (!q.exec()) qWarning() << "insertEvent failed:" << q.lastError().text();
    }
    m_pool->release(db);
}

void AlarmMysqlRepo::updateAck(const QString& alarmId, const QString& user, qint64 ts) {
    auto db = m_pool->acquire();
    QSqlQuery q(db);
    q.prepare("UPDATE alarm_events SET acknowledged=1, ack_user=?, ack_time=? WHERE alarm_id=?");
    q.addBindValue(user);
    q.addBindValue(ts);
    q.addBindValue(alarmId);
    q.exec();
    m_pool->release(db);
}

void AlarmMysqlRepo::updateEvent(const QString& alarmId, const QString& field, const QString& value, qint64 ts) {
    auto db = m_pool->acquire();
    QSqlQuery q(db);
    q.prepare(QString("UPDATE alarm_events SET %1=?, trigger_time=? WHERE alarm_id=?").arg(field));
    q.addBindValue(value);
    q.addBindValue(ts);
    q.addBindValue(alarmId);
    q.exec();
    m_pool->release(db);
}

QVector<AlarmEvent> AlarmMysqlRepo::queryActive() {
    auto db = m_pool->acquire();
    QSqlQuery q(db);
    q.exec("SELECT * FROM alarm_events WHERE active=1 OR state IN (3,4)");
    // Simplified: return empty vector (full deserialization omitted for brevity)
    m_pool->release(db);
    return {};
}

QVector<AlarmEvent> AlarmMysqlRepo::queryEvents(const AlarmFilter& filter, int limit) {
    Q_UNUSED(filter); Q_UNUSED(limit);
    return {};
}

QVector<AlarmEvent> AlarmMysqlRepo::queryHistory(qint64 start, qint64 end, int limit) {
    Q_UNUSED(start); Q_UNUSED(end); Q_UNUSED(limit);
    return {};
}

void AlarmMysqlRepo::insertChangeRecord(const AlarmChangeRecord& r) {
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
    q.exec();
    m_pool->release(db);
}

QVector<AlarmChangeRecord> AlarmMysqlRepo::queryChangeRecords(quint32 tagId, int limit) {
    Q_UNUSED(tagId); Q_UNUSED(limit); return {};
}

QVector<AlarmChangeRecord> AlarmMysqlRepo::queryPendingApprovals() { return {}; }

void AlarmMysqlRepo::updateChangeApproval(int recordId, bool approved, const QString& approver, const QString& rejectReason) {
    auto db = m_pool->acquire();
    QSqlQuery q(db);
    q.prepare("UPDATE alarm_change_log SET approved=?, approver=?, approve_time=?, reject_reason=? WHERE id=?");
    q.addBindValue(approved ? 1 : 0);
    q.addBindValue(approver);
    q.addBindValue(QDateTime::currentMSecsSinceEpoch());
    q.addBindValue(rejectReason);
    q.addBindValue(recordId);
    q.exec();
    m_pool->release(db);
}

void AlarmMysqlRepo::insertKpiSnapshot(const AlarmKpiSnapshot& s) {
    auto db = m_pool->acquire();
    QSqlQuery q(db);
    q.prepare("INSERT INTO alarm_kpi_snapshots (timestamp, alarm_count_10min, avg_per_hour, peak_count_10min, "
        "stale_count, total_active, shelved_count, suppressed_count, health_score, health_grade) "
        "VALUES (?,?,?,?,?,?,?,?,?,?)");
    q.addBindValue(s.timestamp);
    q.addBindValue(s.alarmCount10min);
    q.addBindValue(s.avgPerHour);
    q.addBindValue(s.peakCount10min);
    q.addBindValue(s.staleCount);
    q.addBindValue(s.totalActive);
    q.addBindValue(s.shelvedCount);
    q.addBindValue(s.suppressedCount);
    q.addBindValue(s.systemHealthScore);
    q.addBindValue(s.healthGrade);
    q.exec();
    m_pool->release(db);
}

QVector<AlarmKpiSnapshot> AlarmMysqlRepo::queryKpiHistory(qint64 start, qint64 end, int limit) {
    Q_UNUSED(start); Q_UNUSED(end); Q_UNUSED(limit);
    return {};
}

void AlarmMysqlRepo::purgeOldRecords(int keepDays) {
    auto db = m_pool->acquire();
    qint64 cutoff = QDateTime::currentMSecsSinceEpoch() - static_cast<qint64>(keepDays) * 86400000;
    QSqlQuery q(db);
    q.prepare("DELETE FROM alarm_events WHERE trigger_time < ?");
    q.addBindValue(cutoff);
    q.exec();
    m_pool->release(db);
}
