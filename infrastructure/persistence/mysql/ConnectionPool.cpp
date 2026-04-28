#include "ConnectionPool.h"
#include <QDebug>

ConnectionPool::ConnectionPool(const QString& type, const QString& host, int port,
                                const QString& dbName, const QString& user, const QString& pass, int poolSize)
    : m_type(type), m_host(host), m_port(port), m_dbName(dbName), m_user(user), m_pass(pass), m_poolSize(poolSize) {}

ConnectionPool::~ConnectionPool() {
    for (const auto& name : m_available) {
        QSqlDatabase::removeDatabase(name);
    }
}

QSqlDatabase ConnectionPool::acquire() {
    QMutexLocker lock(&m_mutex);
    if (!m_available.isEmpty()) {
        QString name = m_available.dequeue();
        return QSqlDatabase::database(name);
    }

    QString connName = QString("pool_%1_%2").arg(m_dbName).arg(m_counter++);
    QSqlDatabase db = QSqlDatabase::addDatabase(m_type, connName);
    if (m_type == "QSQLITE") {
        db.setDatabaseName(m_dbName);
    } else {
        db.setHostName(m_host);
        db.setPort(m_port);
        db.setDatabaseName(m_dbName);
        db.setUserName(m_user);
        db.setPassword(m_pass);
        db.setConnectOptions("MYSQL_OPT_RECONNECT=1");
    }
    if (!db.open()) {
        qWarning() << "ConnectionPool: failed to open" << m_dbName << db.lastError().text();
    }
    return db;
}

void ConnectionPool::release(QSqlDatabase db) {
    QMutexLocker lock(&m_mutex);
    QString name = db.connectionName();
    if (m_available.size() < m_poolSize) {
        m_available.enqueue(name);
    } else {
        QSqlDatabase::removeDatabase(name);
    }
}
