#include "OperationMysqlRepo.h"
#include <QSqlQuery>

OperationMysqlRepo::OperationMysqlRepo(std::shared_ptr<ConnectionPool> pool) : m_pool(pool) { ensureTables(); }

void OperationMysqlRepo::ensureTables() {
    auto db = m_pool->acquire();
    QSqlQuery q(db);
    q.exec("CREATE TABLE IF NOT EXISTS operation_log ("
        "id BIGINT AUTO_INCREMENT PRIMARY KEY, username VARCHAR(64), action VARCHAR(128), "
        "detail TEXT, timestamp BIGINT, INDEX idx_user_time (username, timestamp))");
    m_pool->release(db);
}

void OperationMysqlRepo::insertLog(const QString& username, const QString& action, const QString& detail, qint64 timestamp) {
    auto db = m_pool->acquire();
    QSqlQuery q(db);
    q.prepare("INSERT INTO operation_log (username, action, detail, timestamp) VALUES (?,?,?,?)");
    q.addBindValue(username);
    q.addBindValue(action);
    q.addBindValue(detail);
    q.addBindValue(timestamp);
    q.exec();
    m_pool->release(db);
}
