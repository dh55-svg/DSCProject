#pragma once
#include "infrastructure/persistence/IUserRepo.h"
#include "infrastructure/persistence/mysql/ConnectionPool.h"
#include <memory>

class UserMysqlRepo : public IUserRepo {
public:
    explicit UserMysqlRepo(std::shared_ptr<ConnectionPool> pool);
    User loadUser(const QString& username) override;
    QVector<User> loadAll() override;
    void updateSession(const QString& username, qint64 lastLogin) override;
private:
    void ensureTables();
    std::shared_ptr<ConnectionPool> m_pool;
};
