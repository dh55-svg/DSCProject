#include "HistoryMysqlRepo.h"
#include <QSqlQuery>
#include <QSqlError>
#include <QDateTime>
#include <QDebug>

HistoryMysqlRepo::HistoryMysqlRepo(std::shared_ptr<ConnectionPool> pool) : m_pool(pool) { ensureTables(); }

void HistoryMysqlRepo::ensureTables() {
    auto db = m_pool->acquire();
    QSqlQuery q(db);
    q.exec("CREATE TABLE IF NOT EXISTS history_data ("
        "id BIGINT AUTO_INCREMENT PRIMARY KEY, tag_id INT, value DOUBLE, quality INT, timestamp BIGINT, "
        "INDEX idx_tag_time (tag_id, timestamp))");
    m_pool->release(db);
}

void HistoryMysqlRepo::batchInsert(const QVector<HistoryRecord>& records) {
    if (records.isEmpty()) return;
    auto db = m_pool->acquire();
    QSqlQuery q(db);
    q.prepare("INSERT INTO history_data (tag_id, value, quality, timestamp) VALUES (?,?,?,?)");
    for (const auto& r : records) {
        q.addBindValue(r.tagId);
        q.addBindValue(r.value);
        q.addBindValue(r.quality);
        q.addBindValue(r.timestamp);
        q.exec();
    }
    m_pool->release(db);
}

QVector<HistoryRecord> HistoryMysqlRepo::query(quint32 tagId, qint64 startTime, qint64 endTime, int maxPoints) {
    QVector<HistoryRecord> result;
    auto db = m_pool->acquire();
    QSqlQuery q(db);
    q.prepare("SELECT tag_id, value, quality, timestamp FROM history_data "
              "WHERE tag_id=? AND timestamp>=? AND timestamp<=? ORDER BY timestamp ASC LIMIT ?");
    q.addBindValue(tagId);
    q.addBindValue(startTime);
    q.addBindValue(endTime);
    q.addBindValue(maxPoints);
    if (q.exec()) {
        while (q.next()) {
            HistoryRecord r;
            r.tagId = q.value(0).toUInt();
            r.value = q.value(1).toDouble();
            r.quality = q.value(2).toInt();
            r.timestamp = q.value(3).toLongLong();
            result.append(r);
        }
    }
    m_pool->release(db);
    return result;
}

void HistoryMysqlRepo::purgeOldRecords(int keepDays) {
    auto db = m_pool->acquire();
    qint64 cutoff = QDateTime::currentMSecsSinceEpoch() - static_cast<qint64>(keepDays) * 86400000;
    QSqlQuery q(db);
    q.prepare("DELETE FROM history_data WHERE timestamp < ?");
    q.addBindValue(cutoff);
    q.exec();
    m_pool->release(db);
}
