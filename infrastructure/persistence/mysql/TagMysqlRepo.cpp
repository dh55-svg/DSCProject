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

QVector<TagInfo> TagMysqlRepo::loadAll() {
    auto db = m_pool->acquire();
    QSqlQuery q(db);
    q.exec("SELECT * FROM tags");
    QVector<TagInfo> results;
    while (q.next()) {
        TagInfo tag;
        tag.tagId            = q.value("tag_id").toUInt();
        tag.tagName          = q.value("tag_name").toString();
        tag.description      = q.value("description").toString();
        tag.unit             = q.value("unit").toString();
        tag.tagType          = static_cast<TagType>(q.value("tag_type").toInt());
        tag.engHigh          = q.value("eng_high").toFloat();
        tag.engLow           = q.value("eng_low").toFloat();
        tag.highHighLimit    = q.value("hh_limit").toFloat();
        tag.highLimit        = q.value("h_limit").toFloat();
        tag.lowLimit         = q.value("l_limit").toFloat();
        tag.lowLowLimit      = q.value("ll_limit").toFloat();
        tag.deadband         = q.value("deadband").toFloat();
        tag.modbusServerAddr = q.value("modbus_server").toInt();
        tag.modbusRegAddr    = q.value("modbus_reg").toInt();
        tag.modbusRegCount   = q.value("modbus_count").toInt();
        results.append(tag);
    }
    m_pool->release(db);
    return results;
}

void TagMysqlRepo::save(const QVector<TagInfo>& tags) {
    auto db = m_pool->acquire();
    QSqlQuery q(db);
    q.exec("DELETE FROM tags");
    q.prepare("INSERT INTO tags (tag_id, tag_name, description, unit, tag_type, "
        "eng_high, eng_low, hh_limit, h_limit, l_limit, ll_limit, deadband, modbus_server, modbus_reg, modbus_count) "
        "VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)");
    for (const auto& tag : tags) {
        q.addBindValue(tag.tagId); q.addBindValue(tag.tagName); q.addBindValue(tag.description);
        q.addBindValue(tag.unit); q.addBindValue(static_cast<int>(tag.tagType));
        q.addBindValue(tag.engHigh); q.addBindValue(tag.engLow);
        q.addBindValue(tag.highHighLimit); q.addBindValue(tag.highLimit);
        q.addBindValue(tag.lowLimit); q.addBindValue(tag.lowLowLimit);
        q.addBindValue(tag.deadband);
        q.addBindValue(tag.modbusServerAddr); q.addBindValue(tag.modbusRegAddr); q.addBindValue(tag.modbusRegCount);
        if (!q.exec()) qWarning() << "TagMysqlRepo save failed:" << q.lastError().text();
    }
    m_pool->release(db);
}

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
