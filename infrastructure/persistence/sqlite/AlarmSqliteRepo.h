#pragma once
#include "infrastructure/persistence/IAlarmRepo.h"
#include "infrastructure/persistence/mysql/ConnectionPool.h"
#include <memory>

class AlarmSqliteRepo : public IAlarmRepo {
public:
    explicit AlarmSqliteRepo(std::shared_ptr<ConnectionPool> pool);
    void insertEvent(const AlarmEvent& e) override;
    void batchInsertEvents(const QVector<AlarmEvent>& events) override;
    void updateAck(const QString& alarmId, const QString& user, qint64 ts) override;
    void updateEvent(const QString& alarmId, const QString& field, const QString& value, qint64 ts) override;
    QVector<AlarmEvent> queryActive() override;
    QVector<AlarmEvent> queryEvents(const AlarmFilter& filter, int limit) override;
    QVector<AlarmEvent> queryHistory(qint64 start, qint64 end, int limit) override;
    void insertChangeRecord(const AlarmChangeRecord& r) override;
    QVector<AlarmChangeRecord> queryChangeRecords(quint32 tagId, int limit) override;
    QVector<AlarmChangeRecord> queryPendingApprovals() override;
    void updateChangeApproval(int recordId, bool approved, const QString& approver, const QString& rejectReason) override;
    void insertKpiSnapshot(const AlarmKpiSnapshot& s) override;
    QVector<AlarmKpiSnapshot> queryKpiHistory(qint64 start, qint64 end, int limit) override;
    void purgeOldRecords(int keepDays) override;

private:
    void ensureTables();
    std::shared_ptr<ConnectionPool> m_pool;
};
