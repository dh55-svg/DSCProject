#pragma once
#include "export.h"
#include <QObject>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlError>
#include <QMutex>
#include <QDateTime>
#include <QVector>
#include <QStringList>
/**
 * @brief 数据库管理器（MySQL统一存储版）
 *
 * 职责划分：
 * - MySQL：历史时序数据 + 报警记录 + 操作日志（统一存储）
 * - RealtimeDb：内存实时数据（当前值）
 *
 * 为什么MySQL替代InfluxDB：
 * 1. 架构简化：一个数据库系统解决所有需求
 * 2. 运维简单：降低复杂度和成本
 * 3. 性能足够：MySQL性能远超化工DCS实际需求（300倍余量）
 * 4. 统一管理：统一数据访问接口
 *
 * 数据流：
 * ┌──────────┐    ┌──────────┐
 * │ DoubleBuf│───▶│ RealtimeDb│
 * │ 实时显示 │    │ 内存实时  │
 * └──────────┘    └──────────┘
 *      │
 *      └──▶┌──────────┐
 *          │  MySQL   │
 *          │ 历史数据 │
 *          │ 报警记录 │
 *          │ 操作日志 │
 *          └──────────┘
 *
 * 踩坑经验：
 * - MySQL连接8小时超时（wait_timeout），必须做心跳保活
 * - 批量INSERT必须用事务，否则逐条提交性能差100倍
 * - MySQL的max_allowed_packet要调大，否则大批量INSERT失败
 * - 连接断开后必须重建QSqlDatabase，不能复用旧连接
 * - 时序数据表使用分区表提高查询性能
 */
struct HistoryRecord {
	quint32 tagId;
	double value;
	quint64 timestamp;
	quint8 quality;
};
class CORE_EXPORT DatabaseManager :public QObject {
	Q_OBJECT
public:
	static DatabaseManager& instance();
    /**
     * @brief 初始化MySQL数据库连接
     * @param host MySQL服务器地址
     * @param port 端口
     * @param database 数据库名
     * @param username 用户名
     * @param password 密码
     * @return true 连接成功
     */

    bool initialize(const QString& host, int port,
        const QString& database,
        const QString& username,
        const QString& password);

    /**
     * @brief 初始化带SQLite回退，先尝试MySQL，失败则使用SQLite本地存储
     */
    bool initializeWithFallback(const QString& host = "127.0.0.1", int port = 3306,
        const QString& database = "dcs",
        const QString& username = "root",
        const QString& password = "");

    void shutdown();

    bool batchInsertHistory(const QVector<HistoryRecord>& records);

    // 查询历史数据 → 转发到InfluxDBClient
    QVector<HistoryRecord> queryHistory(quint32 tagId,
        const QDateTime& startTime,
        const QDateTime& endTime,
        int maxPoints = 10000);

    // ===== MySQL存储（报警+操作日志+历史归档查询） =====

   // 报警记录
    bool insertAlarmRecord(quint32 tagId, int severity,
        const QString& description,
        double triggerValue, double thresholdValue,
        qint64 timestamp);
    QVector<QVariantMap> queryAlarmHistory(const QDateTime& startTime,
        const QDateTime& endTime,
        int limit = 500);

    // 操作日志
    bool insertOperationLog(const QString& username, const QString& action,
        const QString& detail, qint64 timestamp);
    QVector<QVariantMap> queryOperationLog(const QDateTime& startTime,
        const QDateTime& endTime,
        int limit = 500);

    // 清理过期数据（MySQL中的报警/日志）
    int purgeOldRecords(int keepDays);

    bool isInitialized() const;

    // 检查数据库连接是否有效
    bool ensureConnection();

    // 当前存储后端类型
    QString backendType() const { return m_useSqlite ? QStringLiteral("SQLite") : QStringLiteral("MySQL"); }

signals:
    void mysqlConnectionChanged(bool connected);
private:
    DatabaseManager() = default;
    ~DatabaseManager() override;
    DatabaseManager(const DatabaseManager&) = delete;
    DatabaseManager& operator=(const DatabaseManager&) = delete;

    bool initSqlite();
    bool createTables();
    QSqlDatabase m_db;                  // 数据库连接
    QMutex m_mutex;                     // 写入互斥锁
    bool m_initialized = false;         // 初始化标志
    bool m_useSqlite = false;           // 是否使用SQLite回退

    // 连接参数（用于重连）
    QString m_host;
    int m_port = 3306;
    QString m_database;
    QString m_username;
    QString m_password;
};


