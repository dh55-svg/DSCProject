#include "DatabaseManager.h"
#include "logger.h"
#include <qsqlrecord.h>
#include <qthread.h>
#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>

// ============================================================
// DatabaseManager - ISA-18.2 商业化增强实现
// ============================================================

DatabaseManager& DatabaseManager::instance()
{
    static DatabaseManager instance;
    return instance;
}

bool DatabaseManager::initialize(const QString& host, int port,
                                  const QString& database,
                                  const QString& username,
                                  const QString& password)
{
    QMutexLocker lock(&m_mutex);
    if (m_initialized) return true;

    m_host = host;
    m_port = port;
    m_database = database;
    m_username = username;
    m_password = password;

    m_db = QSqlDatabase::addDatabase("QMYSQL", "dcs_mysql");
    m_db.setHostName(host);
    m_db.setPort(port);
    m_db.setDatabaseName(database);
    m_db.setUserName(username);
    m_db.setPassword(password);
    // 连接选项：自动重连+连接超时10秒
    m_db.setConnectOptions("MYSQL_OPT_RECONNECT=1;MYSQL_OPT_CONNECT_TIMEOUT=10");
    if (!m_db.open()) {
        QString error = m_db.lastError().text();
        LOG_ERROR("Database", QString("MySQL连接失败: %1").arg(error));
        return false;
    }
    if (!createTables()) {
        LOG_ERROR("Database", "创建MySQL数据表失败");
        return false;
    }
    m_initialized = true;
    LOG_INFO("Database", QString("MySQL初始化成功: %1:%2/%3")
        .arg(host).arg(port).arg(database));
    return true;
}

bool DatabaseManager::initializeWithFallback(const QString& host, int port,
    const QString& database,
    const QString& username,
    const QString& password)
{
    // 先尝试MySQL
    if (initialize(host, port, database, username, password)) {
        LOG_INFO("Database", "使用MySQL存储后端");
        m_useSqlite = false;
        return true;
    }

    // MySQL失败，使用SQLite回退
    LOG_WARN("Database", "MySQL不可用，切换到SQLite本地存储");
    m_useSqlite = true;
    bool ok = initSqlite();
    if (ok) {
        LOG_INFO("Database", "SQLite本地存储初始化成功");
    } else {
        LOG_ERROR("Database", "SQLite初始化也失败，将无法持久化数据");
    }
    return ok;
}

bool DatabaseManager::initSqlite()
{
    m_db = QSqlDatabase::addDatabase("QSQLITE", "dcs_sqlite");
    m_db.setDatabaseName("./data/dcs.db");

    // 确保data目录存在
    QDir().mkpath("./data");

    if (!m_db.open()) {
        LOG_ERROR("Database", QString("SQLite打开失败: %1").arg(m_db.lastError().text()));
        return false;
    }

    // SQLite开启WAL模式提升并发性能
    QSqlQuery query(m_db);
    query.exec("PRAGMA journal_mode=WAL");
    query.exec("PRAGMA synchronous=NORMAL");

    if (!createTables()) {
        LOG_ERROR("Database", "SQLite创建数据表失败");
        return false;
    }

    m_initialized = true;
    LOG_INFO("Database", "SQLite初始化成功: ./data/dcs.db");
    return true;
}

void DatabaseManager::shutdown()
{
    QMutexLocker lock(&m_mutex);
    if (m_initialized) {
        m_db.close();
        m_initialized = false;
        LOG_INFO("Database", "数据库连接已关闭");
    }
}

// ============================================================
// 历史时序数据
// ============================================================

bool DatabaseManager::batchInsertHistory(const QVector<HistoryRecord>& records)
{
    if (records.isEmpty()) return true;

    const int MAX_RETRY = 3;
    int retrycount = 0;
    while (retrycount < MAX_RETRY) {
        QMutexLocker lock(&m_mutex);
        if (!ensureConnection()) {
            LOG_ERROR("Database", "数据库连接不可用，无法写入历史数据");
            return false;
        }
        if (!m_db.transaction()) {
            LOG_WARN("Database", QString("开启事务失败: %1").arg(m_db.lastError().text()));
            retrycount++;
            QThread::msleep(retrycount * 100);
            continue;
        }
        QSqlQuery query(m_db);
        query.prepare("INSERT INTO history_data (tag_id, value, quality, timestamp) "
            "VALUES (:tag_id, :value, :quality, :timestamp)");
        bool allSuccess = true;
        for (const auto& record : records) {
            query.bindValue(":tag_id", static_cast<uint>(record.tagId));
            query.bindValue(":value", record.value);
            query.bindValue(":quality", record.quality);
            query.bindValue(":timestamp", static_cast<qlonglong>(record.timestamp));
            if (!query.exec()) {
                m_db.rollback();
                LOG_ERROR("Database", QString("批量插入历史数据失败: %1")
                    .arg(query.lastError().text()));
                allSuccess = false;
                break;
            }
        }
        if (allSuccess) {
            if (!m_db.commit()) {
                m_db.rollback();
                LOG_ERROR("Database", QString("提交事务失败: %1").arg(m_db.lastError().text()));
                retrycount++;
                QThread::msleep(100 * retrycount);
                continue;
            }
            return true;
        }
        retrycount++;
        QThread::msleep(100 * retrycount);
    }
    return false;
}

QVector<HistoryRecord> DatabaseManager::queryHistory(quint32 tagId,
    const QDateTime& startTime,
    const QDateTime& endTime,
    int maxPoints)
{
    QMutexLocker lock(&m_mutex);
    QVector<HistoryRecord> result;
    if (!ensureConnection()) return result;

    QSqlQuery query(m_db);
    query.prepare("SELECT tag_id, value, quality, timestamp FROM history_data "
        "WHERE tag_id = :tag_id AND timestamp BETWEEN :start AND :end "
        "ORDER BY timestamp LIMIT :limit");
    query.bindValue(":tag_id", static_cast<uint>(tagId));
    query.bindValue(":start", static_cast<qlonglong>(startTime.toMSecsSinceEpoch()));
    query.bindValue(":end", static_cast<qlonglong>(endTime.toMSecsSinceEpoch()));
    query.bindValue(":limit", maxPoints);

    if (!query.exec()) {
        LOG_ERROR("Database", QString("查询历史数据失败: %1").arg(query.lastError().text()));
        return result;
    }
    while (query.next()) {
        HistoryRecord record;
        record.tagId = query.value(0).toUInt();
        record.value = query.value(1).toDouble();
        record.quality = query.value(2).value<quint8>();
        record.timestamp = query.value(3).toLongLong();
        result.append(record);
    }
    return result;
}

// ============================================================
// ISA-18.2 报警事件（完整版）
// ============================================================

void DatabaseManager::bindAlarmEventValues(QSqlQuery& query, const AlarmEvent& event)
{
    // 标识字段
    query.bindValue(":alarm_id", event.alarmId);
    query.bindValue(":tag_id", static_cast<uint>(event.tagId));
    query.bindValue(":tag_name", event.tagName);

    // ISA-18.2 报警属性
    query.bindValue(":alarm_limit", static_cast<int>(event.limit));
    query.bindValue(":priority", static_cast<int>(event.priority));
    query.bindValue(":classification", static_cast<int>(event.classification));

    // 触发信息
    query.bindValue(":description", event.description);
    query.bindValue(":trigger_value", static_cast<double>(event.triggerValue));
    query.bindValue(":threshold_value", static_cast<double>(event.thresholdValue));
    query.bindValue(":trigger_time", static_cast<qlonglong>(event.triggerTime));

    // 状态机
    query.bindValue(":state", static_cast<int>(event.state));

    // 确认信息
    query.bindValue(":acknowledged", event.acknowledged ? 1 : 0);
    query.bindValue(":ack_time", static_cast<qlonglong>(event.acknowledgeTime));
    query.bindValue(":ack_user", event.acknowledgeUser);

    // 恢复信息
    query.bindValue(":active", event.active ? 1 : 0);
    query.bindValue(":rtn_time", static_cast<qlonglong>(event.returnToNormalTime));
    query.bindValue(":rtn_ack_time", static_cast<qlonglong>(event.returnAckTime));
    query.bindValue(":return_value", static_cast<double>(event.returnValue));

    // Shelving
    query.bindValue(":shelved", event.shelved ? 1 : 0);
    query.bindValue(":shelved_time", static_cast<qlonglong>(event.shelvedTime));
    query.bindValue(":shelve_reason", event.shelveReason);
    query.bindValue(":shelve_duration", event.shelveDurationSec);
    query.bindValue(":shelve_user", event.shelveUser);

    // Suppression
    query.bindValue(":suppression_type", static_cast<int>(event.suppressionType));
    query.bindValue(":suppression_reason", event.suppressionReason);
    query.bindValue(":suppression_user", event.suppressionUser);
    query.bindValue(":suppression_time", static_cast<qlonglong>(event.suppressionTime));

    // Out-of-Service
    query.bindValue(":out_of_service", event.outOfService ? 1 : 0);
    query.bindValue(":oos_reason", event.outOfServiceReason);
    query.bindValue(":oos_user", event.outOfServiceUser);
    query.bindValue(":work_order", event.workOrderNo);

    // 操作员注释
    query.bindValue(":annotation", event.operatorAnnotation);
    query.bindValue(":annotation_time", static_cast<qlonglong>(event.annotationTime));
    query.bindValue(":annotation_user", event.annotationUser);

    // 区域/分区
    query.bindValue(":area", event.area);
    query.bindValue(":zone", event.zone);

    // 通知策略
    query.bindValue(":notification_type", static_cast<int>(event.notificationType));
    query.bindValue(":notification_count", event.notificationCount);

    // 重复计数
    query.bindValue(":repeat_count", event.repeatCount);
    query.bindValue(":first_trigger_time", static_cast<qlonglong>(event.firstTriggerTime));
}

AlarmEvent DatabaseManager::buildAlarmEventFromQuery(const QSqlQuery& query)
{
    AlarmEvent event;
    int col = 0;

    // 标识字段
    event.alarmId = query.value(col++).toString();
    event.tagId   = query.value(col++).toUInt();
    event.tagName = query.value(col++).toString();

    // ISA-18.2 报警属性
    event.limit           = static_cast<AlarmLimit>(query.value(col++).toInt());
    event.priority        = static_cast<AlarmPriority>(query.value(col++).toInt());
    event.classification  = static_cast<AlarmClassification>(query.value(col++).toInt());

    // 触发信息
    event.description    = query.value(col++).toString();
    event.triggerValue   = static_cast<float>(query.value(col++).toDouble());
    event.thresholdValue = static_cast<float>(query.value(col++).toDouble());
    event.triggerTime    = query.value(col++).toLongLong();

    // 状态机
    event.state = static_cast<AlarmState>(query.value(col++).toInt());

    // 确认信息
    event.acknowledged    = query.value(col++).toInt() != 0;
    event.acknowledgeTime = query.value(col++).toLongLong();
    event.acknowledgeUser = query.value(col++).toString();

    // 恢复信息
    event.active             = query.value(col++).toInt() != 0;
    event.returnToNormalTime = query.value(col++).toLongLong();
    event.returnAckTime      = query.value(col++).toLongLong();
    event.returnValue        = static_cast<float>(query.value(col++).toDouble());

    // Shelving
    event.shelved          = query.value(col++).toInt() != 0;
    event.shelvedTime      = query.value(col++).toLongLong();
    event.shelveReason     = query.value(col++).toString();
    event.shelveDurationSec = query.value(col++).toInt();
    event.shelveUser       = query.value(col++).toString();

    // Suppression
    event.suppressionType   = static_cast<AlarmSuppressionType>(query.value(col++).toInt());
    event.suppressionReason = query.value(col++).toString();
    event.suppressionUser   = query.value(col++).toString();
    event.suppressionTime   = query.value(col++).toLongLong();

    // Out-of-Service
    event.outOfService       = query.value(col++).toInt() != 0;
    event.outOfServiceReason = query.value(col++).toString();
    event.outOfServiceUser   = query.value(col++).toString();
    event.workOrderNo        = query.value(col++).toString();

    // 操作员注释
    event.operatorAnnotation = query.value(col++).toString();
    event.annotationTime     = query.value(col++).toLongLong();
    event.annotationUser     = query.value(col++).toString();

    // 区域/分区
    event.area = query.value(col++).toString();
    event.zone = query.value(col++).toString();

    // 通知策略
    event.notificationType  = static_cast<AlarmNotificationType>(query.value(col++).toInt());
    event.notificationCount = query.value(col++).toInt();

    // 重复计数
    event.repeatCount      = query.value(col++).toInt();
    event.firstTriggerTime = query.value(col++).toLongLong();

    return event;
}

bool DatabaseManager::insertAlarmEvent(const AlarmEvent& event)
{
    QMutexLocker lock(&m_mutex);
    if (!ensureConnection()) {
        LOG_ERROR("Database", "数据库连接不可用，无法写入报警事件");
        return false;
    }

    QSqlQuery query(m_db);
    query.prepare(
        "INSERT INTO alarm_events ("
        "  alarm_id, tag_id, tag_name, alarm_limit, priority, classification,"
        "  description, trigger_value, threshold_value, trigger_time,"
        "  state, acknowledged, ack_time, ack_user,"
        "  active, rtn_time, rtn_ack_time, return_value,"
        "  shelved, shelved_time, shelve_reason, shelve_duration, shelve_user,"
        "  suppression_type, suppression_reason, suppression_user, suppression_time,"
        "  out_of_service, oos_reason, oos_user, work_order,"
        "  annotation, annotation_time, annotation_user,"
        "  area, zone, notification_type, notification_count,"
        "  repeat_count, first_trigger_time"
        ") VALUES ("
        "  :alarm_id, :tag_id, :tag_name, :alarm_limit, :priority, :classification,"
        "  :description, :trigger_value, :threshold_value, :trigger_time,"
        "  :state, :acknowledged, :ack_time, :ack_user,"
        "  :active, :rtn_time, :rtn_ack_time, :return_value,"
        "  :shelved, :shelved_time, :shelve_reason, :shelve_duration, :shelve_user,"
        "  :suppression_type, :suppression_reason, :suppression_user, :suppression_time,"
        "  :out_of_service, :oos_reason, :oos_user, :work_order,"
        "  :annotation, :annotation_time, :annotation_user,"
        "  :area, :zone, :notification_type, :notification_count,"
        "  :repeat_count, :first_trigger_time"
        ")"
    );

    bindAlarmEventValues(query, event);

    if (!query.exec()) {
        LOG_ERROR("Database", QString("写入报警事件失败: %1").arg(query.lastError().text()));
        return false;
    }
    return true;
}

bool DatabaseManager::batchInsertAlarmEvents(const QVector<AlarmEvent>& events)
{
    if (events.isEmpty()) return true;

    QMutexLocker lock(&m_mutex);
    if (!ensureConnection()) {
        LOG_ERROR("Database", "数据库连接不可用，无法批量写入报警事件");
        return false;
    }

    if (!m_db.transaction()) {
        LOG_ERROR("Database", QString("开启事务失败: %1").arg(m_db.lastError().text()));
        return false;
    }

    QSqlQuery query(m_db);
    query.prepare(
        "INSERT INTO alarm_events ("
        "  alarm_id, tag_id, tag_name, alarm_limit, priority, classification,"
        "  description, trigger_value, threshold_value, trigger_time,"
        "  state, acknowledged, ack_time, ack_user,"
        "  active, rtn_time, rtn_ack_time, return_value,"
        "  shelved, shelved_time, shelve_reason, shelve_duration, shelve_user,"
        "  suppression_type, suppression_reason, suppression_user, suppression_time,"
        "  out_of_service, oos_reason, oos_user, work_order,"
        "  annotation, annotation_time, annotation_user,"
        "  area, zone, notification_type, notification_count,"
        "  repeat_count, first_trigger_time"
        ") VALUES ("
        "  :alarm_id, :tag_id, :tag_name, :alarm_limit, :priority, :classification,"
        "  :description, :trigger_value, :threshold_value, :trigger_time,"
        "  :state, :acknowledged, :ack_time, :ack_user,"
        "  :active, :rtn_time, :rtn_ack_time, :return_value,"
        "  :shelved, :shelved_time, :shelve_reason, :shelve_duration, :shelve_user,"
        "  :suppression_type, :suppression_reason, :suppression_user, :suppression_time,"
        "  :out_of_service, :oos_reason, :oos_user, :work_order,"
        "  :annotation, :annotation_time, :annotation_user,"
        "  :area, :zone, :notification_type, :notification_count,"
        "  :repeat_count, :first_trigger_time"
        ")"
    );

    for (const auto& event : events) {
        bindAlarmEventValues(query, event);
        if (!query.exec()) {
            m_db.rollback();
            LOG_ERROR("Database", QString("批量插入报警事件失败: %1").arg(query.lastError().text()));
            return false;
        }
    }

    if (!m_db.commit()) {
        m_db.rollback();
        LOG_ERROR("Database", QString("提交报警事件事务失败: %1").arg(m_db.lastError().text()));
        return false;
    }
    return true;
}

bool DatabaseManager::updateAlarmEvent(const QString& alarmId, const QString& field,
                                        const QString& value, qint64 timestamp)
{
    QMutexLocker lock(&m_mutex);
    if (!ensureConnection()) return false;

    QSqlQuery query(m_db);

    // 根据字段类型构建不同的UPDATE语句
    if (field == "acknowledge") {
        query.prepare("UPDATE alarm_events SET acknowledged=1, ack_time=:ts, "
                      "ack_user=:val, state=2 WHERE alarm_id=:id");
        query.bindValue(":ts", static_cast<qlonglong>(timestamp));
        query.bindValue(":val", value);
        query.bindValue(":id", alarmId);
    } else if (field == "returnToNormal") {
        query.prepare("UPDATE alarm_events SET active=0, rtn_time=:ts, "
                      "return_value=:val, state=3 WHERE alarm_id=:id");
        query.bindValue(":ts", static_cast<qlonglong>(timestamp));
        query.bindValue(":val", value.toDouble());
        query.bindValue(":id", alarmId);
    } else if (field == "shelve") {
        query.prepare("UPDATE alarm_events SET shelved=1, shelved_time=:ts, "
                      "shelve_reason=:val, state=5 WHERE alarm_id=:id");
        query.bindValue(":ts", static_cast<qlonglong>(timestamp));
        query.bindValue(":val", value);
        query.bindValue(":id", alarmId);
    } else if (field == "suppress") {
        query.prepare("UPDATE alarm_events SET suppression_type=1, suppression_reason=:val, "
                      "suppression_time=:ts, state=6 WHERE alarm_id=:id");
        query.bindValue(":ts", static_cast<qlonglong>(timestamp));
        query.bindValue(":val", value);
        query.bindValue(":id", alarmId);
    } else if (field == "outOfService") {
        query.prepare("UPDATE alarm_events SET out_of_service=1, oos_reason=:val, "
                      "suppression_time=:ts, state=7 WHERE alarm_id=:id");
        query.bindValue(":ts", static_cast<qlonglong>(timestamp));
        query.bindValue(":val", value);
        query.bindValue(":id", alarmId);
    } else if (field == "annotation") {
        query.prepare("UPDATE alarm_events SET annotation=:val, "
                      "annotation_time=:ts WHERE alarm_id=:id");
        query.bindValue(":val", value);
        query.bindValue(":ts", static_cast<qlonglong>(timestamp));
        query.bindValue(":id", alarmId);
    } else {
        LOG_WARN("Database", QString("未知的报警更新字段: %1").arg(field));
        return false;
    }

    if (!query.exec()) {
        LOG_ERROR("Database", QString("更新报警事件失败: %1").arg(query.lastError().text()));
        return false;
    }
    return true;
}

QVector<AlarmEvent> DatabaseManager::queryAlarmEvents(const AlarmFilter& filter, int limit)
{
    QMutexLocker lock(&m_mutex);
    QVector<AlarmEvent> result;
    if (!ensureConnection()) return result;

    // 动态构建WHERE子句
    QStringList conditions;
    QString sql = "SELECT "
        "alarm_id, tag_id, tag_name, alarm_limit, priority, classification,"
        "description, trigger_value, threshold_value, trigger_time,"
        "state, acknowledged, ack_time, ack_user,"
        "active, rtn_time, rtn_ack_time, return_value,"
        "shelved, shelved_time, shelve_reason, shelve_duration, shelve_user,"
        "suppression_type, suppression_reason, suppression_user, suppression_time,"
        "out_of_service, oos_reason, oos_user, work_order,"
        "annotation, annotation_time, annotation_user,"
        "area, zone, notification_type, notification_count,"
        "repeat_count, first_trigger_time "
        "FROM alarm_events";

    // 按优先级过滤
    if (!filter.priorities.isEmpty()) {
        QStringList prioStrs;
        for (auto p : filter.priorities) {
            prioStrs << QString::number(static_cast<int>(p));
        }
        conditions << QString("priority IN (%1)").arg(prioStrs.join(","));
    }

    // 按分类过滤
    if (!filter.classifications.isEmpty()) {
        QStringList clsStrs;
        for (auto c : filter.classifications) {
            clsStrs << QString::number(static_cast<int>(c));
        }
        conditions << QString("classification IN (%1)").arg(clsStrs.join(","));
    }

    // 按状态过滤
    if (!filter.states.isEmpty()) {
        QStringList stateStrs;
        for (auto s : filter.states) {
            stateStrs << QString::number(static_cast<int>(s));
        }
        conditions << QString("state IN (%1)").arg(stateStrs.join(","));
    }

    // 按区域过滤
    if (!filter.areas.isEmpty()) {
        QStringList areaStrs;
        for (const auto& a : filter.areas) {
            QString sanitized = a;
            areaStrs << QString("'%1'").arg(sanitized.replace("'", "''"));
        }
        conditions << QString("area IN (%1)").arg(areaStrs.join(","));
    }

    // 按时间范围过滤
    if (filter.fromTime > 0) {
        conditions << QString("trigger_time >= %1").arg(filter.fromTime);
    }
    if (filter.toTime > 0) {
        conditions << QString("trigger_time <= %1").arg(filter.toTime);
    }

    // 按关键字搜索
    if (!filter.keyword.isEmpty()) {
        QString escaped = filter.keyword;
        escaped.replace("'", "''");
        conditions << QString("(tag_name LIKE '%%1%%' OR description LIKE '%%1%%')").arg(escaped);
    }

    // 屏蔽/抑制/停用过滤
    if (!filter.includeShelved) {
        conditions << "shelved = 0";
    }
    if (!filter.includeSuppressed) {
        conditions << "suppression_type = 0";
    }
    if (!filter.includeOutOfService) {
        conditions << "out_of_service = 0";
    }

    // 拼接WHERE
    if (!conditions.isEmpty()) {
        sql += " WHERE " + conditions.join(" AND ");
    }

    sql += " ORDER BY trigger_time DESC";

    // SQLite不支持LIMIT参数绑定，直接拼接
    sql += QString(" LIMIT %1").arg(qBound(1, limit, 10000));

    QSqlQuery query(m_db);
    if (!query.exec(sql)) {
        LOG_ERROR("Database", QString("查询报警事件失败: %1").arg(query.lastError().text()));
        return result;
    }

    while (query.next()) {
        result.append(buildAlarmEventFromQuery(query));
    }
    return result;
}

// ============================================================
// 旧版报警记录接口（兼容保留）
// ============================================================

bool DatabaseManager::insertAlarmRecord(quint32 tagId, int severity,
    const QString& description,
    double triggerValue, double thresholdValue,
    qint64 timestamp)
{
    // 转换为新的 AlarmEvent 接口存储
    AlarmEvent event;
    event.alarmId        = QString("ALM_%1").arg(timestamp);
    event.tagId          = tagId;
    event.limit          = static_cast<AlarmLimit>(severity);
    event.priority       = AlarmPriority::Major;
    event.classification = AlarmClassification::Process;
    event.description    = description;
    event.triggerValue   = static_cast<float>(triggerValue);
    event.thresholdValue = static_cast<float>(thresholdValue);
    event.triggerTime    = timestamp;
    event.state          = AlarmState::ActiveUnacknowledged;
    event.active         = true;

    return insertAlarmEvent(event);
}

QVector<QVariantMap> DatabaseManager::queryAlarmHistory(const QDateTime& startTime,
    const QDateTime& endTime, int limit)
{
    QMutexLocker lock(&m_mutex);
    QVector<QVariantMap> result;
    if (!ensureConnection()) return result;

    // 优先从新表查询，如果新表不存在则回退到旧表
    QSqlQuery query(m_db);
    query.prepare(
        "SELECT alarm_id, tag_id, tag_name, alarm_limit, priority, classification, "
        "description, trigger_value, threshold_value, trigger_time, "
        "acknowledged, ack_time, rtn_time "
        "FROM alarm_events WHERE trigger_time BETWEEN :start AND :end "
        "ORDER BY trigger_time DESC LIMIT :limit");
    query.bindValue(":start", static_cast<qlonglong>(startTime.toMSecsSinceEpoch()));
    query.bindValue(":end", static_cast<qlonglong>(endTime.toMSecsSinceEpoch()));
    query.bindValue(":limit", limit);

    if (query.exec()) {
        while (query.next()) {
            QVariantMap record;
            record["alarm_id"]      = query.value(0);
            record["tag_id"]        = query.value(1);
            record["tag_name"]      = query.value(2);
            record["severity"]      = query.value(3);
            record["priority"]      = query.value(4);
            record["classification"] = query.value(5);
            record["description"]   = query.value(6);
            record["trigger_value"] = query.value(7);
            record["threshold_value"] = query.value(8);
            record["trigger_time"]  = query.value(9);
            record["acknowledged"]  = query.value(10);
            record["acknowledge_time"] = query.value(11);
            record["clear_time"]    = query.value(12);
            result.append(record);
        }
    } else {
        // 回退到旧表
        QSqlQuery query2(m_db);
        query2.prepare("SELECT tag_id, severity, description, trigger_value, threshold_value, "
            "trigger_time, acknowledge_time, clear_time, acknowledged "
            "FROM alarm_history WHERE trigger_time BETWEEN :start AND :end "
            "ORDER BY trigger_time DESC LIMIT :limit");
        query2.bindValue(":start", static_cast<qlonglong>(startTime.toMSecsSinceEpoch()));
        query2.bindValue(":end", static_cast<qlonglong>(endTime.toMSecsSinceEpoch()));
        query2.bindValue(":limit", limit);
        if (query2.exec()) {
            while (query2.next()) {
                QVariantMap record;
                record["tag_id"]          = query2.value(0);
                record["severity"]        = query2.value(1);
                record["description"]     = query2.value(2);
                record["trigger_value"]   = query2.value(3);
                record["threshold_value"] = query2.value(4);
                record["trigger_time"]    = query2.value(5);
                record["acknowledge_time"] = query2.value(6);
                record["clear_time"]      = query2.value(7);
                record["acknowledged"]    = query2.value(8);
                result.append(record);
            }
        }
    }
    return result;
}

// ============================================================
// ISA-18.2 变更审计日志
// ============================================================

void DatabaseManager::bindChangeRecordValues(QSqlQuery& query, const AlarmChangeRecord& record)
{
    query.bindValue(":change_time", static_cast<qlonglong>(record.changeTime));
    query.bindValue(":operator_name", record.operatorName);
    query.bindValue(":tag_id", static_cast<uint>(record.tagId));
    query.bindValue(":field_name", record.fieldName);
    query.bindValue(":old_value", record.oldValue);
    query.bindValue(":new_value", record.newValue);
    query.bindValue(":reason", record.reason);
    query.bindValue(":approved", record.approved ? 1 : 0);
    query.bindValue(":approver", record.approver);
    query.bindValue(":approve_time", static_cast<qlonglong>(record.approveTime));
    query.bindValue(":rejected", record.rejected ? 1 : 0);
    query.bindValue(":reject_reason", record.rejectReason);
    query.bindValue(":work_order", record.workOrderNo);
    query.bindValue(":valid_until", static_cast<qlonglong>(record.validUntil));
    query.bindValue(":auto_reverted", record.autoReverted ? 1 : 0);
    query.bindValue(":session_id", record.sessionId);
    query.bindValue(":workstation", record.workstation);
}

AlarmChangeRecord DatabaseManager::buildChangeRecordFromQuery(const QSqlQuery& query)
{
    AlarmChangeRecord rec;
    int col = 0;

    rec.changeTime   = query.value(col++).toLongLong();
    rec.operatorName = query.value(col++).toString();
    rec.tagId        = query.value(col++).toUInt();
    rec.fieldName    = query.value(col++).toString();
    rec.oldValue     = query.value(col++).toString();
    rec.newValue     = query.value(col++).toString();
    rec.reason       = query.value(col++).toString();
    rec.approved     = query.value(col++).toInt() != 0;
    rec.approver     = query.value(col++).toString();
    rec.approveTime  = query.value(col++).toLongLong();
    rec.rejected     = query.value(col++).toInt() != 0;
    rec.rejectReason = query.value(col++).toString();
    rec.workOrderNo  = query.value(col++).toString();
    rec.validUntil   = query.value(col++).toLongLong();
    rec.autoReverted = query.value(col++).toInt() != 0;
    rec.sessionId    = query.value(col++).toString();
    rec.workstation  = query.value(col++).toString();

    return rec;
}

bool DatabaseManager::insertChangeRecord(const AlarmChangeRecord& record)
{
    QMutexLocker lock(&m_mutex);
    if (!ensureConnection()) {
        LOG_ERROR("Database", "数据库连接不可用，无法写入变更记录");
        return false;
    }

    QSqlQuery query(m_db);
    query.prepare(
        "INSERT INTO alarm_change_log ("
        "  change_time, operator_name, tag_id, field_name, old_value, new_value, reason,"
        "  approved, approver, approve_time, rejected, reject_reason,"
        "  work_order, valid_until, auto_reverted, session_id, workstation"
        ") VALUES ("
        "  :change_time, :operator_name, :tag_id, :field_name, :old_value, :new_value, :reason,"
        "  :approved, :approver, :approve_time, :rejected, :reject_reason,"
        "  :work_order, :valid_until, :auto_reverted, :session_id, :workstation"
        ")"
    );

    bindChangeRecordValues(query, record);

    if (!query.exec()) {
        LOG_ERROR("Database", QString("写入变更记录失败: %1").arg(query.lastError().text()));
        return false;
    }
    return true;
}

bool DatabaseManager::batchInsertChangeRecords(const QVector<AlarmChangeRecord>& records)
{
    if (records.isEmpty()) return true;

    QMutexLocker lock(&m_mutex);
    if (!ensureConnection()) return false;

    if (!m_db.transaction()) return false;

    QSqlQuery query(m_db);
    query.prepare(
        "INSERT INTO alarm_change_log ("
        "  change_time, operator_name, tag_id, field_name, old_value, new_value, reason,"
        "  approved, approver, approve_time, rejected, reject_reason,"
        "  work_order, valid_until, auto_reverted, session_id, workstation"
        ") VALUES ("
        "  :change_time, :operator_name, :tag_id, :field_name, :old_value, :new_value, :reason,"
        "  :approved, :approver, :approve_time, :rejected, :reject_reason,"
        "  :work_order, :valid_until, :auto_reverted, :session_id, :workstation"
        ")"
    );

    for (const auto& rec : records) {
        bindChangeRecordValues(query, rec);
        if (!query.exec()) {
            m_db.rollback();
            LOG_ERROR("Database", QString("批量插入变更记录失败: %1").arg(query.lastError().text()));
            return false;
        }
    }

    if (!m_db.commit()) {
        m_db.rollback();
        return false;
    }
    return true;
}

bool DatabaseManager::updateChangeApproval(int recordId, bool approved,
                                            const QString& approver,
                                            const QString& rejectReason)
{
    QMutexLocker lock(&m_mutex);
    if (!ensureConnection()) return false;

    QSqlQuery query(m_db);
    if (approved) {
        query.prepare("UPDATE alarm_change_log SET approved=1, approver=:approver, "
                      "approve_time=:ts WHERE rowid=:id");
        query.bindValue(":approver", approver);
        query.bindValue(":ts", static_cast<qlonglong>(QDateTime::currentMSecsSinceEpoch()));
    } else {
        query.prepare("UPDATE alarm_change_log SET rejected=1, reject_reason=:reason, "
                      "approver=:approver WHERE rowid=:id");
        query.bindValue(":reason", rejectReason);
        query.bindValue(":approver", approver);
    }
    query.bindValue(":id", recordId);

    if (!query.exec()) {
        LOG_ERROR("Database", QString("更新变更审批状态失败: %1").arg(query.lastError().text()));
        return false;
    }
    return true;
}

QVector<AlarmChangeRecord> DatabaseManager::queryChangeRecords(quint32 tagId, int limit)
{
    QMutexLocker lock(&m_mutex);
    QVector<AlarmChangeRecord> result;
    if (!ensureConnection()) return result;

    QSqlQuery query(m_db);
    if (tagId > 0) {
        query.prepare("SELECT "
            "change_time, operator_name, tag_id, field_name, old_value, new_value, reason,"
            "approved, approver, approve_time, rejected, reject_reason,"
            "work_order, valid_until, auto_reverted, session_id, workstation "
            "FROM alarm_change_log WHERE tag_id = :tag_id "
            "ORDER BY change_time DESC LIMIT :limit");
        query.bindValue(":tag_id", static_cast<uint>(tagId));
    } else {
        query.prepare("SELECT "
            "change_time, operator_name, tag_id, field_name, old_value, new_value, reason,"
            "approved, approver, approve_time, rejected, reject_reason,"
            "work_order, valid_until, auto_reverted, session_id, workstation "
            "FROM alarm_change_log ORDER BY change_time DESC LIMIT :limit");
    }
    query.bindValue(":limit", limit);

    if (query.exec()) {
        while (query.next()) {
            result.append(buildChangeRecordFromQuery(query));
        }
    }
    return result;
}

QVector<AlarmChangeRecord> DatabaseManager::queryPendingApprovals()
{
    QMutexLocker lock(&m_mutex);
    QVector<AlarmChangeRecord> result;
    if (!ensureConnection()) return result;

    QSqlQuery query(m_db);
    query.exec("SELECT "
        "change_time, operator_name, tag_id, field_name, old_value, new_value, reason,"
        "approved, approver, approve_time, rejected, reject_reason,"
        "work_order, valid_until, auto_reverted, session_id, workstation "
        "FROM alarm_change_log WHERE approved=0 AND rejected=0 "
        "ORDER BY change_time DESC");

    while (query.next()) {
        result.append(buildChangeRecordFromQuery(query));
    }
    return result;
}

// ============================================================
// ISA-18.2 KPI 快照
// ============================================================

void DatabaseManager::bindKpiSnapshotValues(QSqlQuery& query, const AlarmKpiSnapshot& snap)
{
    query.bindValue(":timestamp", static_cast<qlonglong>(snap.timestamp));
    query.bindValue(":alarm_count_10min", snap.alarmCount10min);
    query.bindValue(":avg_per_hour", static_cast<double>(snap.avgPerHour));
    query.bindValue(":peak_count_10min", snap.peakCount10min);
    query.bindValue(":stale_count", snap.staleCount);
    query.bindValue(":total_active", snap.totalActive);
    query.bindValue(":shelved_count", snap.shelvedCount);
    query.bindValue(":suppressed_count", snap.suppressedCount);
    query.bindValue(":flood_event_count", snap.floodEventCount);
    query.bindValue(":flood_duration_min", static_cast<double>(snap.floodDurationMin));
    query.bindValue(":avg_ack_time_sec", static_cast<double>(snap.avgAckTimeSec));
    query.bindValue(":chattering_count", snap.chatteringCount);
    query.bindValue(":stale_alarm_percent", snap.staleAlarmPercent);
    query.bindValue(":critical_count", snap.criticalCount);
    query.bindValue(":major_count", snap.majorCount);
    query.bindValue(":minor_count", snap.minorCount);
    query.bindValue(":advisory_count", snap.advisoryCount);
    query.bindValue(":health_score", static_cast<double>(snap.systemHealthScore));
    query.bindValue(":health_grade", snap.healthGrade);

    // Top5列表序列化为JSON
    QJsonArray top5Freq;
    for (const auto& s : snap.top5Frequent) {
        top5Freq.append(s);
    }
    QJsonArray top5Stale;
    for (const auto& s : snap.top5Stale) {
        top5Stale.append(s);
    }
    query.bindValue(":top5_frequent", QJsonDocument(top5Freq).toJson(QJsonDocument::Compact));
    query.bindValue(":top5_stale", QJsonDocument(top5Stale).toJson(QJsonDocument::Compact));
}

AlarmKpiSnapshot DatabaseManager::buildKpiSnapshotFromQuery(const QSqlQuery& query)
{
    AlarmKpiSnapshot snap;
    int col = 0;

    snap.timestamp        = query.value(col++).toLongLong();
    snap.alarmCount10min  = query.value(col++).toInt();
    snap.avgPerHour       = static_cast<float>(query.value(col++).toDouble());
    snap.peakCount10min   = query.value(col++).toInt();
    snap.staleCount       = query.value(col++).toInt();
    snap.totalActive      = query.value(col++).toInt();
    snap.shelvedCount     = query.value(col++).toInt();
    snap.suppressedCount  = query.value(col++).toInt();
    snap.floodEventCount  = query.value(col++).toInt();
    snap.floodDurationMin = static_cast<float>(query.value(col++).toDouble());
    snap.avgAckTimeSec    = static_cast<float>(query.value(col++).toDouble());
    snap.chatteringCount  = query.value(col++).toInt();
    snap.staleAlarmPercent = query.value(col++).toInt();
    snap.criticalCount    = query.value(col++).toInt();
    snap.majorCount       = query.value(col++).toInt();
    snap.minorCount       = query.value(col++).toInt();
    snap.advisoryCount    = query.value(col++).toInt();
    snap.systemHealthScore = static_cast<float>(query.value(col++).toDouble());
    snap.healthGrade      = query.value(col++).toString();

    // 反序列化Top5列表
    QJsonDocument freqDoc = QJsonDocument::fromJson(query.value(col++).toString().toUtf8());
    QJsonDocument staleDoc = QJsonDocument::fromJson(query.value(col++).toString().toUtf8());
    if (freqDoc.isArray()) {
        for (const auto& v : freqDoc.array()) {
            snap.top5Frequent.append(v.toString());
        }
    }
    if (staleDoc.isArray()) {
        for (const auto& v : staleDoc.array()) {
            snap.top5Stale.append(v.toString());
        }
    }

    return snap;
}

bool DatabaseManager::insertKpiSnapshot(const AlarmKpiSnapshot& snapshot)
{
    QMutexLocker lock(&m_mutex);
    if (!ensureConnection()) return false;

    QSqlQuery query(m_db);
    query.prepare(
        "INSERT INTO alarm_kpi_snapshots ("
        "  timestamp, alarm_count_10min, avg_per_hour, peak_count_10min, stale_count,"
        "  total_active, shelved_count, suppressed_count,"
        "  flood_event_count, flood_duration_min, avg_ack_time_sec, chattering_count,"
        "  stale_alarm_percent, critical_count, major_count, minor_count, advisory_count,"
        "  health_score, health_grade, top5_frequent, top5_stale"
        ") VALUES ("
        "  :timestamp, :alarm_count_10min, :avg_per_hour, :peak_count_10min, :stale_count,"
        "  :total_active, :shelved_count, :suppressed_count,"
        "  :flood_event_count, :flood_duration_min, :avg_ack_time_sec, :chattering_count,"
        "  :stale_alarm_percent, :critical_count, :major_count, :minor_count, :advisory_count,"
        "  :health_score, :health_grade, :top5_frequent, :top5_stale"
        ")"
    );

    bindKpiSnapshotValues(query, snapshot);

    if (!query.exec()) {
        LOG_ERROR("Database", QString("写入KPI快照失败: %1").arg(query.lastError().text()));
        return false;
    }
    return true;
}

QVector<AlarmKpiSnapshot> DatabaseManager::queryKpiHistory(const QDateTime& startTime,
    const QDateTime& endTime, int limit)
{
    QMutexLocker lock(&m_mutex);
    QVector<AlarmKpiSnapshot> result;
    if (!ensureConnection()) return result;

    QSqlQuery query(m_db);
    query.prepare("SELECT "
        "timestamp, alarm_count_10min, avg_per_hour, peak_count_10min, stale_count,"
        "total_active, shelved_count, suppressed_count,"
        "flood_event_count, flood_duration_min, avg_ack_time_sec, chattering_count,"
        "stale_alarm_percent, critical_count, major_count, minor_count, advisory_count,"
        "health_score, health_grade, top5_frequent, top5_stale "
        "FROM alarm_kpi_snapshots "
        "WHERE timestamp BETWEEN :start AND :end "
        "ORDER BY timestamp DESC LIMIT :limit");
    query.bindValue(":start", static_cast<qlonglong>(startTime.toMSecsSinceEpoch()));
    query.bindValue(":end", static_cast<qlonglong>(endTime.toMSecsSinceEpoch()));
    query.bindValue(":limit", limit);

    if (query.exec()) {
        while (query.next()) {
            result.append(buildKpiSnapshotFromQuery(query));
        }
    }
    return result;
}

// ============================================================
// 操作日志
// ============================================================

bool DatabaseManager::insertOperationLog(const QString& username, const QString& action,
    const QString& detail, qint64 timestamp)
{
    QMutexLocker locker(&m_mutex);
    if (!ensureConnection()) {
        LOG_ERROR("Database", "数据库连接不可用，无法写入操作日志");
        return false;
    }
    QSqlQuery query(m_db);
    query.prepare("INSERT INTO operation_log (username, action, detail, timestamp) "
        "VALUES (:user, :action, :detail, :time)");
    query.bindValue(":user", username);
    query.bindValue(":action", action);
    query.bindValue(":detail", detail);
    query.bindValue(":time", static_cast<qlonglong>(timestamp));
    if (!query.exec()) {
        LOG_ERROR("Database", QString("写入操作日志失败: %1").arg(query.lastError().text()));
        return false;
    }
    return true;
}

QVector<QVariantMap> DatabaseManager::queryOperationLog(const QDateTime& startTime,
    const QDateTime& endTime, int limit)
{
    QMutexLocker locker(&m_mutex);
    QVector<QVariantMap> result;
    if (!ensureConnection()) return result;

    QSqlQuery query(m_db);
    query.prepare("SELECT username, action, detail, timestamp FROM operation_log "
        "WHERE timestamp BETWEEN :start AND :end ORDER BY timestamp DESC LIMIT :limit");
    query.bindValue(":start", static_cast<qlonglong>(startTime.toMSecsSinceEpoch()));
    query.bindValue(":end", static_cast<qlonglong>(endTime.toMSecsSinceEpoch()));
    query.bindValue(":limit", limit);
    if (query.exec()) {
        while (query.next()) {
            QVariantMap record;
            record["username"]  = query.value(0);
            record["action"]    = query.value(1);
            record["detail"]    = query.value(2);
            record["timestamp"] = query.value(3);
            result.append(record);
        }
    }
    return result;
}

// ============================================================
// 数据维护
// ============================================================

int DatabaseManager::purgeOldRecords(int keepDays)
{
    QMutexLocker locker(&m_mutex);
    if (!ensureConnection()) return 0;

    // ISA-18.2 商业化数据保留策略：不同数据类型不同保留期限
    int historyKeepDays  = (keepDays > 0) ? keepDays : 30;    // 历史时序：30天
    int alarmKeepDays    = qMax(365, keepDays);                // 报警历史：至少1年
    int changeKeepDays   = qMax(730, keepDays);                // 变更日志：至少2年
    int kpiKeepDays      = qMax(90, keepDays);                 // KPI快照：至少90天
    int oplogKeepDays    = qMax(180, keepDays);                // 操作日志：至少180天

    QSqlQuery query(m_db);
    int total = 0;

    // 清理历史时序数据
    qint64 cutoff = QDateTime::currentMSecsSinceEpoch()
                    - static_cast<qint64>(historyKeepDays) * 86400 * 1000;
    query.prepare("DELETE FROM history_data WHERE timestamp < :cutoff");
    query.bindValue(":cutoff", cutoff);
    if (query.exec()) total += query.numRowsAffected();

    // 清理报警事件（ISA-18.2：至少保留1年）
    qint64 alarmCutoff = QDateTime::currentMSecsSinceEpoch()
                         - static_cast<qint64>(alarmKeepDays) * 86400 * 1000;
    query.prepare("DELETE FROM alarm_events WHERE trigger_time < :cutoff");
    query.bindValue(":cutoff", alarmCutoff);
    if (query.exec()) total += query.numRowsAffected();

    // 清理旧版报警历史
    query.prepare("DELETE FROM alarm_history WHERE trigger_time < :cutoff");
    query.bindValue(":cutoff", alarmCutoff);
    if (query.exec()) total += query.numRowsAffected();

    // 清理变更日志（ISA-18.2：至少保留2年）
    qint64 changeCutoff = QDateTime::currentMSecsSinceEpoch()
                          - static_cast<qint64>(changeKeepDays) * 86400 * 1000;
    query.prepare("DELETE FROM alarm_change_log WHERE change_time < :cutoff");
    query.bindValue(":cutoff", changeCutoff);
    if (query.exec()) total += query.numRowsAffected();

    // 清理KPI快照
    qint64 kpiCutoff = QDateTime::currentMSecsSinceEpoch()
                       - static_cast<qint64>(kpiKeepDays) * 86400 * 1000;
    query.prepare("DELETE FROM alarm_kpi_snapshots WHERE timestamp < :cutoff");
    query.bindValue(":cutoff", kpiCutoff);
    if (query.exec()) total += query.numRowsAffected();

    // 清理操作日志
    qint64 oplogCutoff = QDateTime::currentMSecsSinceEpoch()
                         - static_cast<qint64>(oplogKeepDays) * 86400 * 1000;
    query.prepare("DELETE FROM operation_log WHERE timestamp < :cutoff");
    query.bindValue(":cutoff", oplogCutoff);
    if (query.exec()) total += query.numRowsAffected();

    LOG_INFO("Database", QString("清理过期数据完成，共删除 %1 条记录").arg(total));
    return total;
}

QVariantMap DatabaseManager::tableStatistics()
{
    QMutexLocker locker(&m_mutex);
    QVariantMap stats;
    if (!ensureConnection()) return stats;

    QSqlQuery query(m_db);

    auto countTable = [&](const QString& table) -> qint64 {
        if (query.exec(QString("SELECT COUNT(*) FROM %1").arg(table)) && query.next()) {
            return query.value(0).toLongLong();
        }
        return -1;
    };

    stats["history_data"]      = countTable("history_data");
    stats["alarm_events"]      = countTable("alarm_events");
    stats["alarm_change_log"]  = countTable("alarm_change_log");
    stats["alarm_kpi_snapshots"] = countTable("alarm_kpi_snapshots");
    stats["operation_log"]     = countTable("operation_log");
    stats["backend"]           = backendType();

    return stats;
}

bool DatabaseManager::isInitialized() const
{
    return m_initialized;
}

bool DatabaseManager::ensureConnection()
{
    // SQLite不需要重连逻辑
    if (m_useSqlite) {
        return m_db.isOpen();
    }

    if (!m_db.isValid()) {
        m_db = QSqlDatabase::addDatabase("QMYSQL", "dcs_mysql");
        m_db.setHostName(m_host);
        m_db.setPort(m_port);
        m_db.setDatabaseName(m_database);
        m_db.setUserName(m_username);
        m_db.setPassword(m_password);
        m_db.setConnectOptions("MYSQL_OPT_RECONNECT=1;MYSQL_OPT_CONNECT_TIMEOUT=10");
    }
    if (!m_db.isOpen()) {
        if (!m_db.open()) {
            LOG_ERROR("Database", QString("MySQL重连失败: %1").arg(m_db.lastError().text()));
            emit mysqlConnectionChanged(false);
            return false;
        }
    }
    // 执行简单查询验证连接
    QSqlQuery query(m_db);
    if (!query.exec("SELECT 1")) {
        LOG_WARN("Database", "MySQL连接验证失败，正在重连...");
        m_db.close();
        if (!m_db.open()) {
            LOG_ERROR("Database", QString("MySQL重连失败: %1").arg(m_db.lastError().text()));
            emit mysqlConnectionChanged(false);
            return false;
        }
        LOG_INFO("Database", "MySQL重连成功");
        emit mysqlConnectionChanged(true);
    }
    return true;
}

DatabaseManager::~DatabaseManager()
{
    shutdown();
}

// ============================================================
// 建表（ISA-18.2 商业化完整版）
// ============================================================

bool DatabaseManager::createTables()
{
    QSqlQuery query(m_db);

    // ---- 历史时序数据表 ----
    bool ok = query.exec(
        "CREATE TABLE IF NOT EXISTS history_data ("
        "  id BIGINT AUTO_INCREMENT PRIMARY KEY,"
        "  tag_id INT UNSIGNED NOT NULL,"
        "  value DOUBLE NOT NULL,"
        "  quality TINYINT UNSIGNED DEFAULT 0,"
        "  timestamp BIGINT NOT NULL,"
        "  INDEX idx_tag_time (tag_id, timestamp),"
        "  INDEX idx_time (timestamp)"
        ") ENGINE=InnoDB DEFAULT CHARSET=utf8mb4"
    );
    if (!ok) {
        // SQLite回退：去掉ENGINE和AUTO_INCREMENT
        if (m_useSqlite) {
            ok = query.exec(
                "CREATE TABLE IF NOT EXISTS history_data ("
                "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
                "  tag_id INTEGER UNSIGNED NOT NULL,"
                "  value DOUBLE NOT NULL,"
                "  quality INTEGER DEFAULT 0,"
                "  timestamp INTEGER NOT NULL)"
            );
            if (ok) query.exec("CREATE INDEX IF NOT EXISTS idx_tag_time ON history_data(tag_id, timestamp)");
            if (ok) query.exec("CREATE INDEX IF NOT EXISTS idx_time ON history_data(timestamp)");
        }
    }
    if (!ok) {
        LOG_ERROR("Database", QString("创建history_data表失败: %1").arg(query.lastError().text()));
        return false;
    }

    // ---- ISA-18.2 报警事件表（完整版） ----
    ok = query.exec(
        "CREATE TABLE IF NOT EXISTS alarm_events ("
        "  id BIGINT AUTO_INCREMENT PRIMARY KEY,"
        "  alarm_id VARCHAR(64) NOT NULL,"
        "  tag_id INT UNSIGNED NOT NULL,"
        "  tag_name VARCHAR(64),"
        "  alarm_limit TINYINT NOT NULL,"
        "  priority TINYINT NOT NULL,"
        "  classification TINYINT DEFAULT 0,"
        "  description TEXT,"
        "  trigger_value DOUBLE,"
        "  threshold_value DOUBLE,"
        "  trigger_time BIGINT NOT NULL,"
        "  state TINYINT DEFAULT 1,"
        "  acknowledged TINYINT DEFAULT 0,"
        "  ack_time BIGINT DEFAULT 0,"
        "  ack_user VARCHAR(64),"
        "  active TINYINT DEFAULT 1,"
        "  rtn_time BIGINT DEFAULT 0,"
        "  rtn_ack_time BIGINT DEFAULT 0,"
        "  return_value DOUBLE DEFAULT 0,"
        "  shelved TINYINT DEFAULT 0,"
        "  shelved_time BIGINT DEFAULT 0,"
        "  shelve_reason TEXT,"
        "  shelve_duration INT DEFAULT 0,"
        "  shelve_user VARCHAR(64),"
        "  suppression_type TINYINT DEFAULT 0,"
        "  suppression_reason TEXT,"
        "  suppression_user VARCHAR(64),"
        "  suppression_time BIGINT DEFAULT 0,"
        "  out_of_service TINYINT DEFAULT 0,"
        "  oos_reason TEXT,"
        "  oos_user VARCHAR(64),"
        "  work_order VARCHAR(64),"
        "  annotation TEXT,"
        "  annotation_time BIGINT DEFAULT 0,"
        "  annotation_user VARCHAR(64),"
        "  area VARCHAR(64),"
        "  zone VARCHAR(64),"
        "  notification_type TINYINT DEFAULT 2,"
        "  notification_count INT DEFAULT 0,"
        "  repeat_count INT DEFAULT 0,"
        "  first_trigger_time BIGINT DEFAULT 0,"
        "  UNIQUE INDEX idx_alarm_id (alarm_id),"
        "  INDEX idx_ae_tag_time (tag_id, trigger_time),"
        "  INDEX idx_ae_time (trigger_time),"
        "  INDEX idx_ae_state (state),"
        "  INDEX idx_ae_priority (priority)"
        ") ENGINE=InnoDB DEFAULT CHARSET=utf8mb4"
    );
    if (!ok && m_useSqlite) {
        ok = query.exec(
            "CREATE TABLE IF NOT EXISTS alarm_events ("
            "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "  alarm_id TEXT NOT NULL UNIQUE,"
            "  tag_id INTEGER NOT NULL,"
            "  tag_name TEXT,"
            "  alarm_limit INTEGER NOT NULL,"
            "  priority INTEGER NOT NULL,"
            "  classification INTEGER DEFAULT 0,"
            "  description TEXT,"
            "  trigger_value DOUBLE,"
            "  threshold_value DOUBLE,"
            "  trigger_time INTEGER NOT NULL,"
            "  state INTEGER DEFAULT 1,"
            "  acknowledged INTEGER DEFAULT 0,"
            "  ack_time INTEGER DEFAULT 0,"
            "  ack_user TEXT,"
            "  active INTEGER DEFAULT 1,"
            "  rtn_time INTEGER DEFAULT 0,"
            "  rtn_ack_time INTEGER DEFAULT 0,"
            "  return_value DOUBLE DEFAULT 0,"
            "  shelved INTEGER DEFAULT 0,"
            "  shelved_time INTEGER DEFAULT 0,"
            "  shelve_reason TEXT,"
            "  shelve_duration INTEGER DEFAULT 0,"
            "  shelve_user TEXT,"
            "  suppression_type INTEGER DEFAULT 0,"
            "  suppression_reason TEXT,"
            "  suppression_user TEXT,"
            "  suppression_time INTEGER DEFAULT 0,"
            "  out_of_service INTEGER DEFAULT 0,"
            "  oos_reason TEXT,"
            "  oos_user TEXT,"
            "  work_order TEXT,"
            "  annotation TEXT,"
            "  annotation_time INTEGER DEFAULT 0,"
            "  annotation_user TEXT,"
            "  area TEXT,"
            "  zone TEXT,"
            "  notification_type INTEGER DEFAULT 2,"
            "  notification_count INTEGER DEFAULT 0,"
            "  repeat_count INTEGER DEFAULT 0,"
            "  first_trigger_time INTEGER DEFAULT 0)"
        );
        if (ok) {
            query.exec("CREATE INDEX IF NOT EXISTS idx_ae_tag_time ON alarm_events(tag_id, trigger_time)");
            query.exec("CREATE INDEX IF NOT EXISTS idx_ae_time ON alarm_events(trigger_time)");
            query.exec("CREATE INDEX IF NOT EXISTS idx_ae_state ON alarm_events(state)");
            query.exec("CREATE INDEX IF NOT EXISTS idx_ae_priority ON alarm_events(priority)");
        }
    }
    if (!ok) {
        LOG_ERROR("Database", QString("创建alarm_events表失败: %1").arg(query.lastError().text()));
        return false;
    }

    // ---- 旧版报警历史表（兼容保留） ----
    ok = query.exec(
        "CREATE TABLE IF NOT EXISTS alarm_history ("
        "  id BIGINT AUTO_INCREMENT PRIMARY KEY,"
        "  tag_id INT UNSIGNED NOT NULL,"
        "  severity INT NOT NULL,"
        "  description TEXT,"
        "  trigger_value DOUBLE,"
        "  threshold_value DOUBLE,"
        "  trigger_time BIGINT NOT NULL,"
        "  acknowledge_time BIGINT DEFAULT 0,"
        "  clear_time BIGINT DEFAULT 0,"
        "  acknowledged TINYINT DEFAULT 0,"
        "  INDEX idx_alarm_tag_time (tag_id, trigger_time),"
        "  INDEX idx_alarm_time (trigger_time)"
        ") ENGINE=InnoDB DEFAULT CHARSET=utf8mb4"
    );
    if (!ok && m_useSqlite) {
        ok = query.exec(
            "CREATE TABLE IF NOT EXISTS alarm_history ("
            "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "  tag_id INTEGER NOT NULL,"
            "  severity INTEGER NOT NULL,"
            "  description TEXT,"
            "  trigger_value DOUBLE,"
            "  threshold_value DOUBLE,"
            "  trigger_time INTEGER NOT NULL,"
            "  acknowledge_time INTEGER DEFAULT 0,"
            "  clear_time INTEGER DEFAULT 0,"
            "  acknowledged INTEGER DEFAULT 0)"
        );
        if (ok) {
            query.exec("CREATE INDEX IF NOT EXISTS idx_alarm_tag_time ON alarm_history(tag_id, trigger_time)");
            query.exec("CREATE INDEX IF NOT EXISTS idx_alarm_time ON alarm_history(trigger_time)");
        }
    }
    if (!ok) {
        LOG_ERROR("Database", QString("创建alarm_history表失败: %1").arg(query.lastError().text()));
        return false;
    }

    // ---- ISA-18.2 变更审计日志表 ----
    ok = query.exec(
        "CREATE TABLE IF NOT EXISTS alarm_change_log ("
        "  id BIGINT AUTO_INCREMENT PRIMARY KEY,"
        "  change_time BIGINT NOT NULL,"
        "  operator_name VARCHAR(64),"
        "  tag_id INT UNSIGNED NOT NULL,"
        "  field_name VARCHAR(64) NOT NULL,"
        "  old_value TEXT,"
        "  new_value TEXT,"
        "  reason TEXT,"
        "  approved TINYINT DEFAULT 0,"
        "  approver VARCHAR(64),"
        "  approve_time BIGINT DEFAULT 0,"
        "  rejected TINYINT DEFAULT 0,"
        "  reject_reason TEXT,"
        "  work_order VARCHAR(64),"
        "  valid_until BIGINT DEFAULT 0,"
        "  auto_reverted TINYINT DEFAULT 0,"
        "  session_id VARCHAR(128),"
        "  workstation VARCHAR(128),"
        "  INDEX idx_cl_tag (tag_id),"
        "  INDEX idx_cl_time (change_time),"
        "  INDEX idx_cl_approval (approved, rejected)"
        ") ENGINE=InnoDB DEFAULT CHARSET=utf8mb4"
    );
    if (!ok && m_useSqlite) {
        ok = query.exec(
            "CREATE TABLE IF NOT EXISTS alarm_change_log ("
            "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "  change_time INTEGER NOT NULL,"
            "  operator_name TEXT,"
            "  tag_id INTEGER NOT NULL,"
            "  field_name TEXT NOT NULL,"
            "  old_value TEXT,"
            "  new_value TEXT,"
            "  reason TEXT,"
            "  approved INTEGER DEFAULT 0,"
            "  approver TEXT,"
            "  approve_time INTEGER DEFAULT 0,"
            "  rejected INTEGER DEFAULT 0,"
            "  reject_reason TEXT,"
            "  work_order TEXT,"
            "  valid_until INTEGER DEFAULT 0,"
            "  auto_reverted INTEGER DEFAULT 0,"
            "  session_id TEXT,"
            "  workstation TEXT)"
        );
        if (ok) {
            query.exec("CREATE INDEX IF NOT EXISTS idx_cl_tag ON alarm_change_log(tag_id)");
            query.exec("CREATE INDEX IF NOT EXISTS idx_cl_time ON alarm_change_log(change_time)");
            query.exec("CREATE INDEX IF NOT EXISTS idx_cl_approval ON alarm_change_log(approved, rejected)");
        }
    }
    if (!ok) {
        LOG_ERROR("Database", QString("创建alarm_change_log表失败: %1").arg(query.lastError().text()));
        return false;
    }

    // ---- ISA-18.2 KPI 快照表 ----
    ok = query.exec(
        "CREATE TABLE IF NOT EXISTS alarm_kpi_snapshots ("
        "  id BIGINT AUTO_INCREMENT PRIMARY KEY,"
        "  timestamp BIGINT NOT NULL,"
        "  alarm_count_10min INT DEFAULT 0,"
        "  avg_per_hour DOUBLE DEFAULT 0,"
        "  peak_count_10min INT DEFAULT 0,"
        "  stale_count INT DEFAULT 0,"
        "  total_active INT DEFAULT 0,"
        "  shelved_count INT DEFAULT 0,"
        "  suppressed_count INT DEFAULT 0,"
        "  flood_event_count INT DEFAULT 0,"
        "  flood_duration_min DOUBLE DEFAULT 0,"
        "  avg_ack_time_sec DOUBLE DEFAULT 0,"
        "  chattering_count INT DEFAULT 0,"
        "  stale_alarm_percent INT DEFAULT 0,"
        "  critical_count INT DEFAULT 0,"
        "  major_count INT DEFAULT 0,"
        "  minor_count INT DEFAULT 0,"
        "  advisory_count INT DEFAULT 0,"
        "  health_score DOUBLE DEFAULT 100,"
        "  health_grade VARCHAR(4),"
        "  top5_frequent TEXT,"
        "  top5_stale TEXT,"
        "  INDEX idx_kpi_time (timestamp)"
        ") ENGINE=InnoDB DEFAULT CHARSET=utf8mb4"
    );
    if (!ok && m_useSqlite) {
        ok = query.exec(
            "CREATE TABLE IF NOT EXISTS alarm_kpi_snapshots ("
            "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "  timestamp INTEGER NOT NULL,"
            "  alarm_count_10min INTEGER DEFAULT 0,"
            "  avg_per_hour DOUBLE DEFAULT 0,"
            "  peak_count_10min INTEGER DEFAULT 0,"
            "  stale_count INTEGER DEFAULT 0,"
            "  total_active INTEGER DEFAULT 0,"
            "  shelved_count INTEGER DEFAULT 0,"
            "  suppressed_count INTEGER DEFAULT 0,"
            "  flood_event_count INTEGER DEFAULT 0,"
            "  flood_duration_min DOUBLE DEFAULT 0,"
            "  avg_ack_time_sec DOUBLE DEFAULT 0,"
            "  chattering_count INTEGER DEFAULT 0,"
            "  stale_alarm_percent INTEGER DEFAULT 0,"
            "  critical_count INTEGER DEFAULT 0,"
            "  major_count INTEGER DEFAULT 0,"
            "  minor_count INTEGER DEFAULT 0,"
            "  advisory_count INTEGER DEFAULT 0,"
            "  health_score DOUBLE DEFAULT 100,"
            "  health_grade TEXT,"
            "  top5_frequent TEXT,"
            "  top5_stale TEXT)"
        );
        if (ok) {
            query.exec("CREATE INDEX IF NOT EXISTS idx_kpi_time ON alarm_kpi_snapshots(timestamp)");
        }
    }
    if (!ok) {
        LOG_ERROR("Database", QString("创建alarm_kpi_snapshots表失败: %1").arg(query.lastError().text()));
        return false;
    }

    // ---- 操作日志表 ----
    ok = query.exec(
        "CREATE TABLE IF NOT EXISTS operation_log ("
        "  id BIGINT AUTO_INCREMENT PRIMARY KEY,"
        "  username VARCHAR(64) NOT NULL,"
        "  action VARCHAR(128) NOT NULL,"
        "  detail TEXT,"
        "  timestamp BIGINT NOT NULL,"
        "  INDEX idx_oplog_time (timestamp),"
        "  INDEX idx_oplog_user (username)"
        ") ENGINE=InnoDB DEFAULT CHARSET=utf8mb4"
    );
    if (!ok && m_useSqlite) {
        ok = query.exec(
            "CREATE TABLE IF NOT EXISTS operation_log ("
            "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "  username TEXT NOT NULL,"
            "  action TEXT NOT NULL,"
            "  detail TEXT,"
            "  timestamp INTEGER NOT NULL)"
        );
        if (ok) {
            query.exec("CREATE INDEX IF NOT EXISTS idx_oplog_time ON operation_log(timestamp)");
            query.exec("CREATE INDEX IF NOT EXISTS idx_oplog_user ON operation_log(username)");
        }
    }
    if (!ok) {
        LOG_ERROR("Database", QString("创建operation_log表失败: %1").arg(query.lastError().text()));
        return false;
    }

    LOG_INFO("Database", QString("ISA-18.2 数据表创建/验证完成 (%1)").arg(backendType()));
    return true;
}
