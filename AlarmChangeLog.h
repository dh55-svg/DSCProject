#pragma once
#include "export.h"
#include "TagDef.h"
#include <QObject>
#include <QMutex>
#include <QVector>
#include <QDateTime>
#include <QString>

/**
 * @brief 报警变更日志（ISA-18.2 Level 4）
 *
 * 所有报警参数变更必须记录，支持审批流程。
 * 可持久化到数据库或JSON文件。
 */
class BUSINESS_EXPORT AlarmChangeLog : public QObject {
    Q_OBJECT
public:
    static AlarmChangeLog& instance();

    /// 记录一次变更
    void recordChange(const AlarmChangeRecord& record);

    /// 批量记录
    void recordChanges(const QVector<AlarmChangeRecord>& records);

    /// 查询变更历史
    QVector<AlarmChangeRecord> queryChanges(quint32 tagId = 0,
                                            int limit = 100) const;

    /// 查询未审批的变更
    QVector<AlarmChangeRecord> pendingApprovals() const;

    /// 审批一个变更
    bool approve(int index, const QString& approver);

    /// 持久化变更日志到JSON文件
    bool saveToFile(const QString& path);
    bool loadFromFile(const QString& path);

    /// 导出审计报告
    QString generateAuditReport(const QDateTime& from,
                                const QDateTime& to) const;

signals:
    void changeRecorded(const AlarmChangeRecord& record);
    void changeApproved(const QString& field, const QString& operatorName);

    friend class AlarmEngine;

private:
    AlarmChangeLog() = default;
    ~AlarmChangeLog() override = default;
    AlarmChangeLog(const AlarmChangeLog&) = delete;
    AlarmChangeLog& operator=(const AlarmChangeLog&) = delete;

    mutable QMutex m_mutex;
    QVector<AlarmChangeRecord> m_records;  // 最新在前
};
