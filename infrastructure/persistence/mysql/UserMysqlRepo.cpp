#include "UserMysqlRepo.h"
#include <QSqlQuery>

UserMysqlRepo::UserMysqlRepo(std::shared_ptr<ConnectionPool> pool) : m_pool(pool) { ensureTables(); }

void UserMysqlRepo::ensureTables() {
    auto db = m_pool->acquire();
    QSqlQuery q(db);
    q.exec("CREATE TABLE IF NOT EXISTS users ("
        "username VARCHAR(64) PRIMARY KEY, password_hash TEXT, salt TEXT, user_level INT, "
        "full_name VARCHAR(128), active INT, last_login BIGINT, created_time BIGINT)");
    m_pool->release(db);
}

User UserMysqlRepo::loadUser(const QString& username) {
    auto db = m_pool->acquire();
    QSqlQuery q(db);
    q.prepare("SELECT * FROM users WHERE username=?");
    q.addBindValue(username);
    if (q.exec() && q.next()) {
        User u;
        u.username = q.value("username").toString();
        u.passwordHash = q.value("password_hash").toString();
        u.salt = q.value("salt").toString();
        u.userLevel = q.value("user_level").toInt();
        u.fullName = q.value("full_name").toString();
        u.active = q.value("active").toBool();
        m_pool->release(db);
        return u;
    }
    m_pool->release(db);
    return User{};
}

QVector<User> UserMysqlRepo::loadAll() {
    QVector<User> result;
    auto db = m_pool->acquire();
    QSqlQuery q(db);
    q.exec("SELECT * FROM users");
    while (q.next()) {
        User u;
        u.username = q.value("username").toString();
        u.passwordHash = q.value("password_hash").toString();
        u.userLevel = q.value("user_level").toInt();
        result.append(u);
    }
    m_pool->release(db);
    return result;
}

void UserMysqlRepo::updateSession(const QString& username, qint64 lastLogin) {
    auto db = m_pool->acquire();
    QSqlQuery q(db);
    q.prepare("UPDATE users SET last_login=? WHERE username=?");
    q.addBindValue(lastLogin);
    q.addBindValue(username);
    q.exec();
    m_pool->release(db);
}
