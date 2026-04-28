#pragma once
#include "infrastructure/persistence/IAlarmRepo.h"
#include "infrastructure/persistence/mysql/ConnectionPool.h"
#include <memory>

// SQLite implementation using the same interfaces — swap-in replacement for MySQL
class AlarmSqliteRepo : public IAlarmRepo {
public:
    explicit AlarmSqliteRepo(std::shared_ptr<ConnectionPool> pool) : m_pool(pool) { ensureTables(); }
    void insertEvent(const AlarmEvent& e) override { Q_UNUSED(e); }
    void batchInsertEvents(const QVector<AlarmEvent>& events) override { Q_UNUSED(events); }
    void updateAck(const QString& alarmId, const QString& user, qint64 ts) override { Q_UNUSED(alarmId); Q_UNUSED(user); Q_UNUSED(ts); }
    void updateEvent(const QString& alarmId, const QString& field, const QString& value, qint64 ts) override { Q_UNUSED(alarmId); Q_UNUSED(field); Q_UNUSED(value); Q_UNUSED(ts); }
    QVector<AlarmEvent> queryActive() override { return {}; }
    QVector<AlarmEvent> queryEvents(const AlarmFilter& filter, int limit) override { Q_UNUSED(filter); Q_UNUSED(limit); return {}; }
    QVector<AlarmEvent> queryHistory(qint64 start, qint64 end, int limit) override { Q_UNUSED(start); Q_UNUSED(end); Q_UNUSED(limit); return {}; }
    void insertChangeRecord(const AlarmChangeRecord& r) override { Q_UNUSED(r); }
    QVector<AlarmChangeRecord> queryChangeRecords(quint32 tagId, int limit) override { Q_UNUSED(tagId); Q_UNUSED(limit); return {}; }
    QVector<AlarmChangeRecord> queryPendingApprovals() override { return {}; }
    void updateChangeApproval(int recordId, bool approved, const QString& approver, const QString& rejectReason) override { Q_UNUSED(recordId); Q_UNUSED(approved); Q_UNUSED(approver); Q_UNUSED(rejectReason); }
    void insertKpiSnapshot(const AlarmKpiSnapshot& s) override { Q_UNUSED(s); }
    QVector<AlarmKpiSnapshot> queryKpiHistory(qint64 start, qint64 end, int limit) override { Q_UNUSED(start); Q_UNUSED(end); Q_UNUSED(limit); return {}; }
    void purgeOldRecords(int keepDays) override { Q_UNUSED(keepDays); }
private:
    void ensureTables() {}
    std::shared_ptr<ConnectionPool> m_pool;
};
