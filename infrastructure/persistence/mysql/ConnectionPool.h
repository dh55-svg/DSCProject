#pragma once
#include <QString>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlError>
#include <QMutex>
#include <QQueue>
#include <memory>

class ConnectionPool {
public:
    ConnectionPool(const QString& type, const QString& host, int port,
                   const QString& dbName, const QString& user, const QString& pass, int poolSize = 5);
    ~ConnectionPool();

    QSqlDatabase acquire();
    void release(QSqlDatabase db);

    static ConnectionPool* mysql(const QString& host, int port, const QString& dbName,
                                  const QString& user, const QString& pass) {
        return new ConnectionPool("QMYSQL", host, port, dbName, user, pass);
    }

    static ConnectionPool* sqlite(const QString& dbPath) {
        return new ConnectionPool("QSQLITE", "", 0, dbPath, "", "", 1);
    }

private:
    QString m_type, m_host, m_dbName, m_user, m_pass;
    int m_port, m_poolSize;
    QQueue<QString> m_available;
    QMutex m_mutex;
    int m_counter = 0;
};
