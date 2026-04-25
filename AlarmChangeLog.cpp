#include "AlarmChangeLog.h"
#include "logger.h"
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QTextStream>
#include <QDir>

AlarmChangeLog& AlarmChangeLog::instance()
{
    static AlarmChangeLog instance;
    return instance;
}

void AlarmChangeLog::recordChange(const AlarmChangeRecord& record)
{
    QMutexLocker lock(&m_mutex);
    AlarmChangeRecord rec = record;
    rec.changeTime = QDateTime::currentMSecsSinceEpoch();
    m_records.prepend(rec);  // 最新在前
    if (m_records.size() > 10000) m_records.removeLast();

    lock.unlock();
    emit changeRecorded(rec);
    LOG_INFO("AlarmChangeLog",
             QString("变更记录: tagId=%1, 字段=%2, %3→%4, 操作人=%5")
                 .arg(record.tagId).arg(record.fieldName)
                 .arg(record.oldValue).arg(record.newValue)
                 .arg(record.operatorName));
}

void AlarmChangeLog::recordChanges(const QVector<AlarmChangeRecord>& records)
{
    for (const auto& r : records) recordChange(r);
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
        if (!rec.approved) {
            result.append(rec);
        }
    }
    return result;
}

bool AlarmChangeLog::approve(int index, const QString& approver)
{
    QMutexLocker lock(&m_mutex);
    if (index < 0 || index >= m_records.size()) return false;
    if (m_records[index].approved) return false;
    m_records[index].approved = true;
    m_records[index].approver = approver;
    lock.unlock();
    emit changeApproved(m_records[index].fieldName, m_records[index].operatorName);
    return true;
}

bool AlarmChangeLog::saveToFile(const QString& path)
{
    QMutexLocker lock(&m_mutex);
    QJsonArray arr;
    for (const auto& rec : m_records) {
        QJsonObject obj;
        obj["changeTime"]   = rec.changeTime;
        obj["operatorName"] = rec.operatorName;
        obj["tagId"]        = static_cast<qint64>(rec.tagId);
        obj["fieldName"]    = rec.fieldName;
        obj["oldValue"]     = rec.oldValue;
        obj["newValue"]     = rec.newValue;
        obj["reason"]       = rec.reason;
        obj["approved"]     = rec.approved;
        obj["approver"]     = rec.approver;
        arr.append(obj);
    }

    QJsonDocument doc(arr);
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly)) return false;
    file.write(doc.toJson(QJsonDocument::Indented));
    return true;
}

bool AlarmChangeLog::loadFromFile(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return false;

    QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    if (!doc.isArray()) return false;

    QMutexLocker lock(&m_mutex);
    m_records.clear();
    for (const auto& val : doc.array()) {
        QJsonObject obj = val.toObject();
        AlarmChangeRecord rec;
        rec.changeTime   = obj["changeTime"].toInteger();
        rec.operatorName = obj["operatorName"].toString();
        rec.tagId        = static_cast<quint32>(obj["tagId"].toInteger());
        rec.fieldName    = obj["fieldName"].toString();
        rec.oldValue     = obj["oldValue"].toString();
        rec.newValue     = obj["newValue"].toString();
        rec.reason       = obj["reason"].toString();
        rec.approved     = obj["approved"].toBool();
        rec.approver     = obj["approver"].toString();
        m_records.append(rec);
    }
    return true;
}

QString AlarmChangeLog::generateAuditReport(const QDateTime& from, const QDateTime& to) const
{
    QMutexLocker lock(&m_mutex);
    qint64 fromMs = from.toMSecsSinceEpoch();
    qint64 toMs   = to.toMSecsSinceEpoch();

    QString report;
    QTextStream out(&report);
    out << "========================================\n"
        << "ISA-18.2 报警参数变更审计报告\n"
        << "期间: " << from.toString("yyyy-MM-dd HH:mm")
        << " ~ " << to.toString("yyyy-MM-dd HH:mm") << "\n"
        << "========================================\n\n";

    int count = 0;
    for (const auto& rec : m_records) {
        if (rec.changeTime < fromMs || rec.changeTime > toMs) continue;
        count++;
        QDateTime dt = QDateTime::fromMSecsSinceEpoch(rec.changeTime);
        out << "[" << dt.toString("yyyy-MM-dd HH:mm:ss") << "]\n"
            << "  操作人: " << rec.operatorName << "\n"
            << "  位号ID: " << rec.tagId << "\n"
            << "  修改字段: " << rec.fieldName << "\n"
            << "  变更: " << rec.oldValue << " → " << rec.newValue << "\n"
            << "  原因: " << rec.reason << "\n"
            << "  审批: " << (rec.approved ? rec.approver : "待审批") << "\n\n";
    }
    out << "总计: " << count << " 条变更记录\n";
    return report;
}
