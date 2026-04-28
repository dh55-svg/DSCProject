#pragma once
#include "infrastructure/persistence/IOperationRepo.h"
#include "infrastructure/persistence/mysql/ConnectionPool.h"
#include <memory>

class OperationMysqlRepo : public IOperationRepo {
public:
    explicit OperationMysqlRepo(std::shared_ptr<ConnectionPool> pool);
    void insertLog(const QString& username, const QString& action, const QString& detail, qint64 timestamp) override;
private:
    void ensureTables();
    std::shared_ptr<ConnectionPool> m_pool;
};
