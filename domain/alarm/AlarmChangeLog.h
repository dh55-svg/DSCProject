#pragma once
#include <QObject>
#include <QMutex>
#include <QVector>
#include "domain/alarm/AlarmEvent.h"

class AlarmChangeLog : public QObject {
    Q_OBJECT
public:
    explicit AlarmChangeLog(QObject* parent = nullptr) : QObject(parent) {}

    void recordChange(const AlarmChangeRecord& record);
    void recordChanges(const QVector<AlarmChangeRecord>& records);
    QVector<AlarmChangeRecord> queryChanges(quint32 tagId = 0, int limit = 100) const;
    QVector<AlarmChangeRecord> pendingApprovals() const;
    bool approve(int index, const QString& approver);
    bool saveToFile(const QString& path);
    bool loadFromFile(const QString& path);
    QString generateAuditReport(const QDateTime& from, const QDateTime& to) const;

signals:
    void changeRecorded(const AlarmChangeRecord& record);
    void changeApproved(const QString& field, const QString& operatorName);

private:
    mutable QMutex m_mutex;
    QVector<AlarmChangeRecord> m_records;
};
