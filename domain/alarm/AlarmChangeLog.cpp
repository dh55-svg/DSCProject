#include "AlarmChangeLog.h"
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>

void AlarmChangeLog::recordChange(const AlarmChangeRecord& record) {
    QMutexLocker lock(&m_mutex);
    AlarmChangeRecord r = record;
    r.changeTime = QDateTime::currentMSecsSinceEpoch();
    m_records.prepend(r);
    if (m_records.size() > 10000) m_records.removeLast();
    lock.unlock();
    emit changeRecorded(r);
}

void AlarmChangeLog::recordChanges(const QVector<AlarmChangeRecord>& records) {
    for (const auto& r : records) recordChange(r);
}

QVector<AlarmChangeRecord> AlarmChangeLog::queryChanges(quint32 tagId, int limit) const {
    QMutexLocker lock(&m_mutex);
    QVector<AlarmChangeRecord> result;
    for (const auto& r : m_records) {
        if (tagId == 0 || r.tagId == tagId) result.append(r);
        if (result.size() >= limit) break;
    }
    return result;
}

QVector<AlarmChangeRecord> AlarmChangeLog::pendingApprovals() const {
    QMutexLocker lock(&m_mutex);
    QVector<AlarmChangeRecord> result;
    for (const auto& r : m_records) {
        if (!r.approved && !r.rejected) result.append(r);
    }
    return result;
}

bool AlarmChangeLog::approve(int index, const QString& approver) {
    QMutexLocker lock(&m_mutex);
    if (index < 0 || index >= m_records.size()) return false;
    m_records[index].approved = true;
    m_records[index].approver = approver;
    m_records[index].approveTime = QDateTime::currentMSecsSinceEpoch();
    QString field = m_records[index].fieldName;
    QString op = m_records[index].operatorName;
    lock.unlock();
    emit changeApproved(field, op);
    return true;
}

bool AlarmChangeLog::saveToFile(const QString& path) {
    QMutexLocker lock(&m_mutex);
    QJsonArray arr;
    for (const auto& r : m_records) {
        QJsonObject o;
        o["changeTime"] = r.changeTime;
        o["operatorName"] = r.operatorName;
        o["tagId"] = (int)r.tagId;
        o["fieldName"] = r.fieldName;
        o["oldValue"] = r.oldValue;
        o["newValue"] = r.newValue;
        o["reason"] = r.reason;
        o["approved"] = r.approved;
        o["approver"] = r.approver;
        o["approveTime"] = r.approveTime;
        o["rejected"] = r.rejected;
        o["rejectReason"] = r.rejectReason;
        o["workOrderNo"] = r.workOrderNo;
        arr.append(o);
    }
    QJsonDocument doc(arr);
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly)) return false;
    file.write(doc.toJson(QJsonDocument::Indented));
    file.close();
    return true;
}

bool AlarmChangeLog::loadFromFile(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return false;
    QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    file.close();
    if (!doc.isArray()) return false;

    QMutexLocker lock(&m_mutex);
    m_records.clear();
    for (const auto& val : doc.array()) {
        QJsonObject o = val.toObject();
        AlarmChangeRecord r;
        r.changeTime = o["changeTime"].toVariant().toLongLong();
        r.operatorName = o["operatorName"].toString();
        r.tagId = o["tagId"].toInt();
        r.fieldName = o["fieldName"].toString();
        r.oldValue = o["oldValue"].toString();
        r.newValue = o["newValue"].toString();
        r.reason = o["reason"].toString();
        r.approved = o["approved"].toBool();
        r.approver = o["approver"].toString();
        r.approveTime = o["approveTime"].toVariant().toLongLong();
        r.rejected = o["rejected"].toBool();
        r.rejectReason = o["rejectReason"].toString();
        r.workOrderNo = o["workOrderNo"].toString();
        m_records.append(r);
    }
    return true;
}

QString AlarmChangeLog::generateAuditReport(const QDateTime& from, const QDateTime& to) const {
    QMutexLocker lock(&m_mutex);
    QString report = QString("=== 报警变更审计报告 ===\n时间段: %1 ~ %2\n总变更数: %3\n\n")
        .arg(from.toString("yyyy-MM-dd HH:mm"), to.toString("yyyy-MM-dd HH:mm"))
        .arg(m_records.size());

    int count = 0;
    for (const auto& r : m_records) {
        if (count++ >= 200) break;
        report += QString("[%1] %2 修改 %3: %4 → %5 (%6)\n")
            .arg(QDateTime::fromMSecsSinceEpoch(r.changeTime).toString("yyyy-MM-dd HH:mm"))
            .arg(r.operatorName, r.fieldName, r.oldValue, r.newValue, r.reason);
    }
    return report;
}
