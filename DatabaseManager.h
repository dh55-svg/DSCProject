#pragma once
#include "export.h"
#include "TagDef.h"
#include <QObject>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlError>
#include <QMutex>
#include <QDateTime>
#include <QVector>
#include <QStringList>

/**
 * @brief 数据库管理器（MySQL统一存储版 - ISA-18.2 商业化增强）
 *
 * 职责划分：
 * - MySQL/SQLite：历史时序数据 + ISA-18.2报警记录 + 操作日志 + 变更日志 + KPI快照
 * - DoubleBuffer：内存实时数据（当前值，RCU无锁读取）
 *
 * ISA-18.2 数据持久化要求：
 * 1. 报警历史必须包含完整状态机信息（触发/确认/恢复/关闭时间）
 * 2. 所有参数变更必须有审计记录（谁/何时/改什么/为什么/谁审批）
 * 3. KPI数据必须可追溯（用于事后分析和合规审计）
 * 4. 数据保留策略必须可配置（不同数据类型不同保留期限）
 *
 * 数据流：
 * ┌──────────┐
 * │ DoubleBuf│
 * │ RCU无锁  │
 * └────┬─────┘
 *      │ 30分钟内：内存环形缓存（TagHistoryRing）
 *      │ 30分钟后：批量写入 MySQL
 *      ▼
 * ┌──────────────────┐
 * │  MySQL / SQLite  │
 * │ 历史时序数据      │
 * │ ISA-18.2报警记录  │
 * │ 操作日志          │
 * │ 变更审计日志      │
 * │ KPI快照           │
 * └──────────────────┘
 *
 * 踩坑经验：
 * - MySQL连接8小时超时（wait_timeout），必须做心跳保活
 * - 批量INSERT必须用事务，否则逐条提交性能差100倍
 * - MySQL的max_allowed_packet要调大，否则大批量INSERT失败
 * - 连接断开后必须重建QSqlDatabase，不能复用旧连接
 * - 时序数据表使用分区表提高查询性能
 * - SQLite的AUTO_INCREMENT不需要指定，MySQL需要BIGINT AUTO_INCREMENT
 * - 两种数据库的SQL方言差异通过 isSqlite() 判断处理
 */
struct HistoryRecord {
    quint32 tagId;
    double value;
    quint64 timestamp;
    quint8 quality;
};

class CORE_EXPORT DatabaseManager : public QObject {
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

    // ===== 历史时序数据 =====

    bool batchInsertHistory(const QVector<HistoryRecord>& records);

    QVector<HistoryRecord> queryHistory(quint32 tagId,
        const QDateTime& startTime,
        const QDateTime& endTime,
        int maxPoints = 10000);

    // ===== ISA-18.2 报警记录（完整版） =====

    /**
     * @brief 插入ISA-18.2完整报警记录
     *
     * 包含完整状态机信息：触发/确认/恢复/关闭时间、
     * 优先级/分类/区域/屏蔽/抑制/停用状态、操作员注释等。
     * 替代旧的 insertAlarmRecord 简版接口。
     */
    bool insertAlarmEvent(const AlarmEvent& event);

    /**
     * @brief 批量插入报警事件（事务提交，高性能）
     */
    bool batchInsertAlarmEvents(const QVector<AlarmEvent>& events);

    /**
     * @brief 更新报警事件的确认/恢复/关闭状态
     *
     * @param alarmId 报警唯一ID
     * @param field 更新字段："acknowledge"/"returnToNormal"/"shelve"/"suppress"/"outOfService"
     * @param value 字段值（JSON格式或简单字符串）
     * @param timestamp 时间戳
     */
    bool updateAlarmEvent(const QString& alarmId, const QString& field,
                          const QString& value, qint64 timestamp);

    /**
     * @brief 按过滤条件查询报警历史（ISA-18.2 报警汇总）
     */
    QVector<AlarmEvent> queryAlarmEvents(const AlarmFilter& filter, int limit = 500);

    /**
     * @brief 旧版报警记录接口（兼容保留）
     */
    bool insertAlarmRecord(quint32 tagId, int severity,
        const QString& description,
        double triggerValue, double thresholdValue,
        qint64 timestamp);

    QVector<QVariantMap> queryAlarmHistory(const QDateTime& startTime,
        const QDateTime& endTime,
        int limit = 500);

    // ===== ISA-18.2 变更审计日志 =====

    /**
     * @brief 插入变更记录（ISA-18.2 Level 4 审计追踪）
     */
    bool insertChangeRecord(const AlarmChangeRecord& record);

    /**
     * @brief 批量插入变更记录
     */
    bool batchInsertChangeRecords(const QVector<AlarmChangeRecord>& records);

    /**
     * @brief 更新变更记录的审批状态
     */
    bool updateChangeApproval(int recordId, bool approved,
                              const QString& approver,
                              const QString& rejectReason = QString());

    /**
     * @brief 查询变更记录
     */
    QVector<AlarmChangeRecord> queryChangeRecords(quint32 tagId = 0,
                                                   int limit = 100);

    /**
     * @brief 查询待审批的变更记录
     */
    QVector<AlarmChangeRecord> queryPendingApprovals();

    // ===== ISA-18.2 KPI 快照 =====

    /**
     * @brief 存储KPI快照（定时存储，用于趋势分析和合规审计）
     */
    bool insertKpiSnapshot(const AlarmKpiSnapshot& snapshot);

    /**
     * @brief 查询KPI快照历史
     */
    QVector<AlarmKpiSnapshot> queryKpiHistory(const QDateTime& startTime,
                                               const QDateTime& endTime,
                                               int limit = 1440);

    // ===== 操作日志 =====

    bool insertOperationLog(const QString& username, const QString& action,
        const QString& detail, qint64 timestamp);
    QVector<QVariantMap> queryOperationLog(const QDateTime& startTime,
        const QDateTime& endTime,
        int limit = 500);

    // ===== 数据维护 =====

    /**
     * @brief 清理过期数据（按数据类型分别配置保留天数）
     *
     * ISA-18.2 商业化要求：
     * - 报警历史：至少保留1年（合规审计）
     * - 变更日志：至少保留2年（法规要求）
     * - KPI快照：至少保留90天
     * - 操作日志：至少保留180天
     * - 历史时序：至少保留30天
     *
     * @param keepDays 保留天数（0=使用默认值）
     * @return 删除的记录数
     */
    int purgeOldRecords(int keepDays = 0);

    /**
     * @brief 获取各表的记录数统计（用于健康检查）
     */
    QVariantMap tableStatistics();

    bool isInitialized() const;

    // 检查数据库连接是否有效
    bool ensureConnection();

    // 当前存储后端类型
    QString backendType() const { return m_useSqlite ? QStringLiteral("SQLite") : QStringLiteral("MySQL"); }

    // 是否使用SQLite
    bool isSqlite() const { return m_useSqlite; }

signals:
    void mysqlConnectionChanged(bool connected);

private:
    DatabaseManager() = default;
    ~DatabaseManager() override;
    DatabaseManager(const DatabaseManager&) = delete;
    DatabaseManager& operator=(const DatabaseManager&) = delete;

    bool initSqlite();
    bool createTables();

    /**
     * @brief 将AlarmEvent转换为数据库INSERT参数
     */
    void bindAlarmEventValues(QSqlQuery& query, const AlarmEvent& event);

    /**
     * @brief 从查询结果构建AlarmEvent
     */
    AlarmEvent buildAlarmEventFromQuery(const QSqlQuery& query);

    /**
     * @brief 将AlarmChangeRecord转换为数据库INSERT参数
     */
    void bindChangeRecordValues(QSqlQuery& query, const AlarmChangeRecord& record);

    /**
     * @brief 从查询结果构建AlarmChangeRecord
     */
    AlarmChangeRecord buildChangeRecordFromQuery(const QSqlQuery& query);

    /**
     * @brief 将AlarmKpiSnapshot转换为数据库INSERT参数
     */
    void bindKpiSnapshotValues(QSqlQuery& query, const AlarmKpiSnapshot& snapshot);

    /**
     * @brief 从查询结果构建AlarmKpiSnapshot
     */
    AlarmKpiSnapshot buildKpiSnapshotFromQuery(const QSqlQuery& query);

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
