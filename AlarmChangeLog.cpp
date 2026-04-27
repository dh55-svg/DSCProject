#include "AlarmChangeLog.h"
#include "logger.h"
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QDateTime>

// ============================================================
// AlarmChangeLog - ISA-18.2 Level 4 变更日志（含审批流程）
// ============================================================

AlarmChangeLog& AlarmChangeLog::instance()
{
    static AlarmChangeLog inst;
    return inst;
}

void AlarmChangeLog::recordChange(const AlarmChangeRecord& record)
{
    QMutexLocker lock(&m_mutex);
    m_records.prepend(record);

    // 限制内存中保留的记录数（最多10000条）
    if (m_records.size() > 10000) {
        m_records.resize(10000);
    }

    LOG_INFO("AlarmChangeLog",
             QString("变更记录: tagId=%1, 字段=%2, %3→%4, 操作人=%5, 原因=%6")
                 .arg(record.tagId)
                 .arg(record.fieldName)
                 .arg(record.oldValue)
                 .arg(record.newValue)
                 .arg(record.operatorName)
                 .arg(record.reason));
}

void AlarmChangeLog::recordChanges(const QVector<AlarmChangeRecord>& records)
{
    QMutexLocker lock(&m_mutex);
    for (const auto& rec : records) {
        m_records.prepend(rec);
    }
    if (m_records.size() > 10000) {
        m_records.resize(10000);
    }
}

QVector<AlarmChangeRecord> AlarmChangeLog::queryChanges(quint32 tagId, int limit) const
{
    QMutexLocker lock(&m_mutex);
    QVector<AlarmChangeRecord> result;

    for (const auto& rec : m_records) {
        if (tagId == 0 || rec.tagId == tagId) {
            result.append(rec);
            if (result.size() >= limit) break;
        }
    }

    return result;
}

QVector<AlarmChangeRecord> AlarmChangeLog::pendingApprovals() const
{
    QMutexLocker lock(&m_mutex);
    QVector<AlarmChangeRecord> result;

    for (const auto& rec : m_records) {
        // 未审批且未驳回的变更
        if (!rec.approved && !rec.rejected) {
            result.append(rec);
        }
    }

    return result;
}

bool AlarmChangeLog::approve(int index, const QString& approver)
{
    QMutexLocker lock(&m_mutex);

    if (index < 0 || index >= m_records.size()) return false;

    auto& rec = m_records[index];
    if (rec.approved || rec.rejected) return false;

    rec.approved = true;
    rec.approver = approver;
    rec.approveTime = QDateTime::currentMSecsSinceEpoch();

    LOG_INFO("AlarmChangeLog",
             QString("变更审批通过: tagId=%1, 字段=%2, 审批人=%3")
                 .arg(rec.tagId).arg(rec.fieldName).arg(approver));

    return true;
}

bool AlarmChangeLog::saveToFile(const QString& path)
{
    QMutexLocker lock(&m_mutex);

    QJsonArray arr;
    for (const auto& rec : m_records) {
        QJsonObject obj;
        obj["changeTime"]    = QString::number(rec.changeTime);
        obj["operatorName"]  = rec.operatorName;
        obj["tagId"]         = static_cast<int>(rec.tagId);
        obj["fieldName"]     = rec.fieldName;
        obj["oldValue"]      = rec.oldValue;
        obj["newValue"]      = rec.newValue;
        obj["reason"]        = rec.reason;
        obj["approved"]      = rec.approved;
        obj["approver"]      = rec.approver;
        obj["approveTime"]   = QString::number(rec.approveTime);
        obj["rejected"]      = rec.rejected;
        obj["rejectReason"]  = rec.rejectReason;
        obj["workOrderNo"]   = rec.workOrderNo;
        obj["validUntil"]    = QString::number(rec.validUntil);
        obj["autoReverted"]  = rec.autoReverted;
        obj["sessionId"]     = rec.sessionId;
        obj["workstation"]   = rec.workstation;
        arr.append(obj);
    }

    QJsonDocument doc(arr);
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        LOG_ERROR("AlarmChangeLog", QString("保存变更日志失败: %1").arg(path));
        return false;
    }

    file.write(doc.toJson(QJsonDocument::Indented));
    file.close();

    LOG_INFO("AlarmChangeLog", QString("变更日志已保存: %1 (%2条记录)")
                 .arg(path).arg(m_records.size()));
    return true;
}

bool AlarmChangeLog::loadFromFile(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        LOG_WARN("AlarmChangeLog", QString("加载变更日志失败: %1").arg(path));
        return false;
    }

    QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    file.close();

    if (!doc.isArray()) return false;

    QMutexLocker lock(&m_mutex);
    m_records.clear();

    QJsonArray arr = doc.array();
    for (const auto& val : arr) {
        QJsonObject obj = val.toObject();
        AlarmChangeRecord rec;
        rec.changeTime    = obj["changeTime"].toString().toLongLong();
        rec.operatorName  = obj["operatorName"].toString();
        rec.tagId         = static_cast<quint32>(obj["tagId"].toInt());
        rec.fieldName     = obj["fieldName"].toString();
        rec.oldValue      = obj["oldValue"].toString();
        rec.newValue      = obj["newValue"].toString();
        rec.reason        = obj["reason"].toString();
        rec.approved      = obj["approved"].toBool();
        rec.approver      = obj["approver"].toString();
        rec.approveTime   = obj["approveTime"].toString().toLongLong();
        rec.rejected      = obj["rejected"].toBool();
        rec.rejectReason  = obj["rejectReason"].toString();
        rec.workOrderNo   = obj["workOrderNo"].toString();
        rec.validUntil    = obj["validUntil"].toString().toLongLong();
        rec.autoReverted  = obj["autoReverted"].toBool();
        rec.sessionId     = obj["sessionId"].toString();
        rec.workstation   = obj["workstation"].toString();
        m_records.append(rec);
    }

    LOG_INFO("AlarmChangeLog", QString("变更日志已加载: %1 (%2条记录)")
                 .arg(path).arg(m_records.size()));
    return true;
}

QString AlarmChangeLog::generateAuditReport(const QDateTime& from,
                                             const QDateTime& to) const
{
    QMutexLocker lock(&m_mutex);

    qint64 fromMs = from.toMSecsSinceEpoch();
    qint64 toMs   = to.toMSecsSinceEpoch();

    QString report;
    report += "========================================\n";
    report += "  ISA-18.2 报警变更审计报告\n";
    report += "========================================\n";
    report += QString("报告期间: %1 ~ %2\n")
                 .arg(from.toString("yyyy-MM-dd HH:mm:ss"))
                 .arg(to.toString("yyyy-MM-dd HH:mm:ss"));
    report += QString("生成时间: %1\n\n")
                 .arg(QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss"));

    int totalChanges = 0;
    int approvedChanges = 0;
    int pendingChanges = 0;
    int rejectedChanges = 0;
    QMap<QString, int> changesByField;
    QMap<QString, int> changesByOperator;

    for (const auto& rec : m_records) {
        if (rec.changeTime < fromMs || rec.changeTime > toMs) continue;

        totalChanges++;

        if (rec.approved) approvedChanges++;
        else if (rec.rejected) rejectedChanges++;
        else pendingChanges++;

        changesByField[rec.fieldName]++;
        if (!rec.operatorName.isEmpty()) {
            changesByOperator[rec.operatorName]++;
        }
    }

    // 统计摘要
    report += "=== 统计摘要 ===\n";
    report += QString("变更总数: %1\n").arg(totalChanges);
    report += QString("已审批: %1\n").arg(approvedChanges);
    report += QString("待审批: %1\n").arg(pendingChanges);
    report += QString("已驳回: %1\n\n").arg(rejectedChanges);

    // 按字段分类统计
    report += "=== 按字段分类 ===\n";
    for (auto it = changesByField.begin(); it != changesByField.end(); ++it) {
        report += QString("  %1: %2次\n").arg(it.key()).arg(it.value());
    }
    report += "\n";

    // 按操作人分类统计
    report += "=== 按操作人分类 ===\n";
    for (auto it = changesByOperator.begin(); it != changesByOperator.end(); ++it) {
        report += QString("  %1: %2次\n").arg(it.key()).arg(it.value());
    }
    report += "\n";

    // 详细变更记录
    report += "=== 详细变更记录 ===\n";
    int shown = 0;
    for (const auto& rec : m_records) {
        if (rec.changeTime < fromMs || rec.changeTime > toMs) continue;
        if (shown >= 200) {
            report += QString("... (仅显示前200条，共%1条)\n").arg(totalChanges);
            break;
        }

        QString status = rec.approved ? "已审批" : (rec.rejected ? "已驳回" : "待审批");
        report += QString("[%1] tagId=%2, %3: %4→%5, 操作人=%6, 原因=%7, 状态=%8")
                      .arg(QDateTime::fromMSecsSinceEpoch(rec.changeTime).toString("yyyy-MM-dd HH:mm:ss"))
                      .arg(rec.tagId)
                      .arg(rec.fieldName)
                      .arg(rec.oldValue)
                      .arg(rec.newValue)
                      .arg(rec.operatorName)
                      .arg(rec.reason)
                      .arg(status);
        if (rec.approved) {
            report += QString(", 审批人=%1").arg(rec.approver);
        }
        if (!rec.workOrderNo.isEmpty()) {
            report += QString(", 工单=%1").arg(rec.workOrderNo);
        }
        report += "\n";
        shown++;
    }

    report += "\n========================================\n";
    report += "  报告结束\n";
    report += "========================================\n";

    return report;
}
