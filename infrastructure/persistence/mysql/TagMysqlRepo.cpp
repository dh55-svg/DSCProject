#include "TagMysqlRepo.h"
#include <QSqlQuery>
#include <QDebug>

TagMysqlRepo::TagMysqlRepo(std::shared_ptr<ConnectionPool> pool) : m_pool(pool) { ensureTables(); }

void TagMysqlRepo::ensureTables() {
    auto db = m_pool->acquire();
    QSqlQuery q(db);
    q.exec("CREATE TABLE IF NOT EXISTS tags ("
        "tag_id INT PRIMARY KEY, tag_name VARCHAR(128), description TEXT, unit VARCHAR(32), "
        "tag_type INT, eng_high DOUBLE, eng_low DOUBLE, hh_limit DOUBLE, h_limit DOUBLE, "
        "l_limit DOUBLE, ll_limit DOUBLE, deadband DOUBLE, modbus_server INT, modbus_reg INT, modbus_count INT)");
    m_pool->release(db);
}

QVector<TagInfo> TagMysqlRepo::loadAll() { return {}; } // DB-driven config deferred
void TagMysqlRepo::save(const QVector<TagInfo>& tags) { Q_UNUSED(tags); }

void TagMysqlRepo::add(const TagInfo& tag) {
    auto db = m_pool->acquire();
    QSqlQuery q(db);
    q.prepare("INSERT OR REPLACE INTO tags (tag_id, tag_name, description, unit, tag_type, "
        "eng_high, eng_low, hh_limit, h_limit, l_limit, ll_limit, deadband, modbus_server, modbus_reg, modbus_count) "
        "VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)");
    q.addBindValue(tag.tagId); q.addBindValue(tag.tagName); q.addBindValue(tag.description);
    q.addBindValue(tag.unit); q.addBindValue(static_cast<int>(tag.tagType));
    q.addBindValue(tag.engHigh); q.addBindValue(tag.engLow);
    q.addBindValue(tag.highHighLimit); q.addBindValue(tag.highLimit);
    q.addBindValue(tag.lowLimit); q.addBindValue(tag.lowLowLimit);
    q.addBindValue(tag.deadband);
    q.addBindValue(tag.modbusServerAddr); q.addBindValue(tag.modbusRegAddr); q.addBindValue(tag.modbusRegCount);
    q.exec();
    m_pool->release(db);
}

void TagMysqlRepo::remove(quint32 tagId) {
    auto db = m_pool->acquire();
    QSqlQuery q(db);
    q.prepare("DELETE FROM tags WHERE tag_id=?");
    q.addBindValue(tagId);
    q.exec();
    m_pool->release(db);
}

void TagMysqlRepo::update(quint32 tagId, const TagInfo& tag) {
    remove(tagId);
    add(tag);
}
