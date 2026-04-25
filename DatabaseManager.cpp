#include "DatabaseManager.h"
#include "logger.h"
#include <qsqlrecord.h>
#include <qthread.h>
#include <QDir>

DatabaseManager& DatabaseManager::instance()
{
    static DatabaseManager instance;
    return instance;
}

bool DatabaseManager::initialize(const QString& host, int port, const QString& database, const QString& username, const QString& password)
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
        // 连接失败不阻止启动，ensureConnection会自动重试
        return false;
    }
    if (!createTables())
    {
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
    if (m_initialized)
    {
        m_db.close();
        m_initialized = false;
        LOG_INFO("Database", "MySQL连接已关闭");
    }
}

bool DatabaseManager::batchInsertHistory(const QVector<HistoryRecord>& records)
{
    if (records.isEmpty())
    {
        return true;
    }
    const int MAX_RETRY = 3;
    int retrycount = 0;
    while (retrycount < MAX_RETRY) {
        QMutexLocker lock(&m_mutex);
        if (!ensureConnection()) {
            LOG_ERROR("Database", "MySQL连接不可用，无法写入历史数据");
            return false;
        }
        if (!m_db.transaction())
        {
            LOG_WARN("Database", QString("开启事务失败: %1").arg(m_db.lastError().text()));
            retrycount++;
            QThread::msleep(retrycount * 100);
            continue;
        }
        QSqlQuery query(m_db);
        query.prepare("INSERT INTO history_data (tag_id, value, quality, timestamp) "
            "VALUES (:tag_id, :value, :quality, :timestamp)");
        bool allSuccess = true;
        int successCount = 0;
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
            successCount++;
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

QVector<HistoryRecord> DatabaseManager::queryHistory(quint32 tagId, const QDateTime& startTime, const QDateTime& endTime, int maxPoints)
{
    QMutexLocker lock(&m_mutex);
    QVector<HistoryRecord> result;
    if (!ensureConnection())
    {
        return result;
    }
    QSqlQuery query(m_db);
    query.prepare("SELECT tag_id, value, quality, timestamp FROM history_data "
        "WHERE tag_id = :tag_id AND timestamp BETWEEN :start AND :end "
        "ORDER BY timestamp LIMIT :limit");
    query.bindValue(":tag_id", static_cast<uint>(tagId));
    query.bindValue(":start", static_cast<qlonglong>(startTime.toMSecsSinceEpoch()));
    query.bindValue(":end", static_cast<qlonglong>(endTime.toMSecsSinceEpoch()));
    query.bindValue(":limit", maxPoints);

    if (!query.exec())
    {
        LOG_ERROR("Database", QString("查询历史数据失败: %1").arg(query.lastError().text()));
        return result;
    }
    while (query.next())
    {
        HistoryRecord record;
        record.tagId = query.value(0).toUInt();
        record.value = query.value(1).toDouble();
        record.quality = query.value(2).value<quint8>();
        record.timestamp = query.value(3).toLongLong();
        result.append(record);
    }
    return result;
}

bool DatabaseManager::insertAlarmRecord(quint32 tagId, int severity, const QString& description, double triggerValue, double thresholdValue, qint64 timestamp)
{
    QMutexLocker locker(&m_mutex);

    if (!ensureConnection()) {
        LOG_ERROR("Database", "MySQL连接不可用，无法写入报警记录");
        return false;
    }

    QSqlQuery query(m_db);
    query.prepare("INSERT INTO alarm_history "
        "(tag_id, severity, description, trigger_value, threshold_value, trigger_time) "
        "VALUES (:tag_id, :severity, :desc, :trig_val, :thresh_val, :time)");
    query.bindValue(":tag_id", static_cast<uint>(tagId));
    query.bindValue(":severity", severity);
    query.bindValue(":desc", description);
    query.bindValue(":trig_val", triggerValue);
    query.bindValue(":thresh_val", thresholdValue);
    query.bindValue(":time", static_cast<qlonglong>(timestamp));
    if (!query.exec()) {
        LOG_ERROR("Database", QString("写入报警记录失败: %1").arg(query.lastError().text()));
        return false;
    }
    return true;
}

QVector<QVariantMap> DatabaseManager::queryAlarmHistory(const QDateTime& startTime, const QDateTime& endTime, int limit)
{
    QMutexLocker locker(&m_mutex);
    QVector<QVariantMap> result;
    if (!ensureConnection()) {
        return result;
    }
    QSqlQuery query(m_db);
    query.prepare("SELECT tag_id, severity, description, trigger_value, threshold_value, "
        "trigger_time, acknowledge_time, clear_time, acknowledged "
        "FROM alarm_history WHERE trigger_time BETWEEN :start AND :end "
        "ORDER BY trigger_time DESC LIMIT :limit");
    query.bindValue(":start", static_cast<qlonglong>(startTime.toMSecsSinceEpoch()));
    query.bindValue(":end", static_cast<qlonglong>(endTime.toMSecsSinceEpoch()));
    query.bindValue(":limit", limit);
    if (query.exec()) {
        while (query.next()) {
            QVariantMap record;
            record["tag_id"] = query.value(0);
            record["severity"] = query.value(1);
            record["description"] = query.value(2);
            record["trigger_value"] = query.value(3);
            record["threshold_value"] = query.value(4);
            record["trigger_time"] = query.value(5);
            record["acknowledge_time"] = query.value(6);
            record["clear_time"] = query.value(7);
            record["acknowledged"] = query.value(8);
            result.append(record);
        }
    }
    return result;
}

bool DatabaseManager::insertOperationLog(const QString& username, const QString& action, const QString& detail, qint64 timestamp)
{
    QMutexLocker locker(&m_mutex);

    if (!ensureConnection()) {
        LOG_ERROR("Database", "MySQL连接不可用，无法写入操作日志");
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

QVector<QVariantMap> DatabaseManager::queryOperationLog(const QDateTime& startTime, const QDateTime& endTime, int limit)
{
    QMutexLocker locker(&m_mutex);
    QVector<QVariantMap> result;
    if (!ensureConnection()) {
        return result;
    }
    QSqlQuery query(m_db);
    query.prepare("SELECT username, action, detail, timestamp FROM operation_log "
        "WHERE timestamp BETWEEN :start AND :end ORDER BY timestamp DESC LIMIT :limit");
    query.bindValue(":start", static_cast<qlonglong>(startTime.toMSecsSinceEpoch()));
    query.bindValue(":end", static_cast<qlonglong>(endTime.toMSecsSinceEpoch()));
    query.bindValue(":limit", limit);
    if (query.exec()) {
        while (query.next()) {
            QVariantMap record;
            record["username"] = query.value(0);
            record["action"] = query.value(1);
            record["detail"] = query.value(2);
            record["timestamp"] = query.value(3);
            result.append(record);
        }
    }
    return result;
}

int DatabaseManager::purgeOldRecords(int keepDays)
{
    QMutexLocker locker(&m_mutex);
    if (!ensureConnection()) {
        return 0;
    }
    qint64 cutoff = QDateTime::currentMSecsSinceEpoch() - static_cast<qint64>(keepDays) * 86400 * 1000;
    QSqlQuery query(m_db);
    int total = 0;

    // 清理历史数据
    query.prepare("DELETE FROM history_data WHERE timestamp < :cutoff");
    query.bindValue(":cutoff", cutoff);
    if (query.exec()) {
        total += query.numRowsAffected();
    }

    // 清理报警记录
    query.prepare("DELETE FROM alarm_history WHERE trigger_time < :cutoff");
    query.bindValue(":cutoff", cutoff);
    if (query.exec()) {
        total += query.numRowsAffected();
    }

    // 清理操作日志
    query.prepare("DELETE FROM operation_log WHERE timestamp < :cutoff");
    query.bindValue(":cutoff", cutoff);
    if (query.exec()) {
        total += query.numRowsAffected();
    }

    LOG_INFO("Database", QString("清理过期数据完成，共删除 %1 条记录（保留 %2 天）")
        .arg(total).arg(keepDays));
    return total;
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

bool DatabaseManager::createTables()
{
    QSqlQuery query(m_db);
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
    if (!ok)
    {
        LOG_ERROR("Database", QString("创建history_data表失败: %1")
            .arg(query.lastError().text()));
        return false;
    }

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
    if (!ok) {
        LOG_ERROR("Database", QString("创建alarm_history表失败: %1")
            .arg(query.lastError().text()));
        return false;
    }

    // 操作日志表
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
    if (!ok) {
        LOG_ERROR("Database", QString("创建operation_log表失败: %1")
            .arg(query.lastError().text()));
        return false;
    }

    LOG_INFO("Database", "MySQL数据表创建/验证完成");
    return true;
}
