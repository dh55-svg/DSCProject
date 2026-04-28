#pragma once
#include "infrastructure/persistence/IHistoryRepo.h"
#include "infrastructure/persistence/mysql/ConnectionPool.h"
#include <memory>

class HistoryMysqlRepo : public IHistoryRepo {
public:
    explicit HistoryMysqlRepo(std::shared_ptr<ConnectionPool> pool);
    void batchInsert(const QVector<HistoryRecord>& records) override;
    QVector<HistoryRecord> query(quint32 tagId, qint64 startTime, qint64 endTime, int maxPoints) override;
    void purgeOldRecords(int keepDays) override;
private:
    void ensureTables();
    std::shared_ptr<ConnectionPool> m_pool;
};
