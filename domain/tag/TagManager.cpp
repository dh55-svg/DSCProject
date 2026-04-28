#include "TagManager.h"
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>

TagManager::TagManager(ITagRepo& repo, ILogger* logger)
    : m_repo(repo), m_logger(logger) {}

bool TagManager::addTag(const TagInfo& tag) {
    QWriteLocker lock(&m_rwlock);
    if (m_tags.contains(tag.tagId)) return false;

    m_tags.insert(tag.tagId, tag);
    m_nameIndex.insert(tag.tagName, tag.tagId);
    quint32 modbusKey = (static_cast<quint32>(tag.modbusServerAddr) << 16) | static_cast<quint32>(tag.modbusRegAddr);
    m_modbusAddrIndex.insert(modbusKey, tag.tagId);
    int deviceId = tag.tagId >> 24;
    m_deviceIndex[deviceId].append(tag.tagId);

    if (m_logger) m_logger->info(QString("添加位号: %1 ID=%2").arg(tag.tagName).arg(tag.tagId));
    emit tagAdded(tag.tagId);
    return true;
}

bool TagManager::removeTag(quint32 tagId) {
    QWriteLocker lock(&m_rwlock);
    auto it = m_tags.find(tagId);
    if (it == m_tags.end()) return false;

    m_nameIndex.remove(it->tagName);
    quint32 modbusKey = (static_cast<quint32>(it->modbusServerAddr) << 16) | static_cast<quint32>(it->modbusRegAddr);
    m_modbusAddrIndex.remove(modbusKey);
    int deviceId = tagId >> 24;
    if (m_deviceIndex.contains(deviceId)) {
        m_deviceIndex[deviceId].removeOne(tagId);
        if (m_deviceIndex[deviceId].isEmpty()) m_deviceIndex.remove(deviceId);
    }
    m_tags.erase(it);

    if (m_logger) m_logger->info(QString("移除位号: ID=%1").arg(tagId));
    emit tagRemoved(tagId);
    return true;
}

TagInfo TagManager::getTag(quint32 tagId) const {
    QReadLocker lock(&m_rwlock);
    return m_tags.value(tagId);
}

TagInfo TagManager::getTagByName(const QString& tagName) const {
    QReadLocker lock(&m_rwlock);
    quint32 id = m_nameIndex.value(tagName, 0);
    return m_tags.value(id);
}

QList<TagInfo> TagManager::getAllTags() const {
    QReadLocker lock(&m_rwlock);
    return m_tags.values();
}

QStringList TagManager::getAllTagNames() const {
    QReadLocker lock(&m_rwlock);
    return m_nameIndex.keys();
}

int TagManager::tagCount() const {
    QReadLocker lock(&m_rwlock);
    return m_tags.size();
}

QPair<float, float> TagManager::getRange(quint32 tagId) const {
    QReadLocker lock(&m_rwlock);
    auto it = m_tags.constFind(tagId);
    if (it != m_tags.constEnd()) return {it->engLow, it->engHigh};
    return {0.0f, 100.0f};
}

QString TagManager::getUnit(quint32 tagId) const {
    QReadLocker lock(&m_rwlock);
    return m_tags.value(tagId).unit;
}

TagManager::AlarmLimits TagManager::getAlarmLimits(quint32 tagId) const {
    QReadLocker lock(&m_rwlock);
    auto it = m_tags.constFind(tagId);
    if (it != m_tags.constEnd()) {
        return {it->highHighLimit, it->highLimit, it->lowLimit, it->lowLowLimit, it->deadband};
    }
    return {};
}

TagManager::ModbusMapping TagManager::getModbusMapping(quint32 tagId) const {
    QReadLocker lock(&m_rwlock);
    auto it = m_tags.constFind(tagId);
    if (it != m_tags.constEnd()) {
        return {it->modbusServerAddr, it->modbusRegAddr, it->modbusRegCount};
    }
    return {};
}

quint32 TagManager::findTagByModbusAddr(int serverAddr, int regAddr) const {
    QReadLocker lock(&m_rwlock);
    quint32 modbusKey = (static_cast<quint32>(serverAddr) << 16) | static_cast<quint32>(regAddr);
    return m_modbusAddrIndex.value(modbusKey, 0);
}

bool TagManager::updateTag(quint32 tagId, const TagInfo& tag) {
    QWriteLocker lock(&m_rwlock);
    auto it = m_tags.find(tagId);
    if (it == m_tags.end()) return false;

    if (it->tagName != tag.tagName) {
        m_nameIndex.remove(it->tagName);
        m_nameIndex.insert(tag.tagName, tagId);
    }
    quint32 oldKey = (static_cast<quint32>(it->modbusServerAddr) << 16) | static_cast<quint32>(it->modbusRegAddr);
    quint32 newKey = (static_cast<quint32>(tag.modbusServerAddr) << 16) | static_cast<quint32>(tag.modbusRegAddr);
    if (oldKey != newKey) {
        m_modbusAddrIndex.remove(oldKey);
        m_modbusAddrIndex.insert(newKey, tagId);
    }
    *it = tag;
    emit configChanged(tagId);
    return true;
}

bool TagManager::updateAlarmLimits(quint32 tagId, const AlarmLimits& limits) {
    QWriteLocker lock(&m_rwlock);
    auto it = m_tags.find(tagId);
    if (it == m_tags.end()) return false;
    it->highHighLimit = limits.highHigh;
    it->highLimit = limits.high;
    it->lowLimit = limits.low;
    it->lowLowLimit = limits.lowLow;
    it->deadband = limits.deadband;
    emit configChanged(tagId);
    return true;
}

bool TagManager::updateRange(quint32 tagId, float engLow, float engHigh) {
    QWriteLocker lock(&m_rwlock);
    auto it = m_tags.find(tagId);
    if (it == m_tags.end()) return false;
    it->engLow = engLow;
    it->engHigh = engHigh;
    emit configChanged(tagId);
    return true;
}

void TagManager::addTags(const QVector<TagInfo>& tags) {
    for (const auto& tag : tags) addTag(tag);
}

void TagManager::clear() {
    QWriteLocker lock(&m_rwlock);
    m_tags.clear();
    m_nameIndex.clear();
    m_modbusAddrIndex.clear();
    m_deviceIndex.clear();
}

QVector<quint32> TagManager::getTagsByDevice(int deviceId) const {
    QReadLocker lock(&m_rwlock);
    return m_deviceIndex.value(deviceId);
}

bool TagManager::loadFromJson(const QString& jsonPath) {
    QFile file(jsonPath);
    if (!file.open(QIODevice::ReadOnly)) return false;

    QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    file.close();
    if (!doc.isArray()) return false;

    QJsonArray tagsArray = doc.array();
    QVector<TagInfo> tags;
    tags.reserve(tagsArray.size());

    for (const QJsonValue& val : tagsArray) {
        QJsonObject obj = val.toObject();
        TagInfo tag;
        tag.tagId = static_cast<quint32>(obj["tagId"].toInt());
        tag.tagName = obj["tagName"].toString();
        tag.description = obj["description"].toString();
        tag.unit = obj["unit"].toString();
        QString typeStr = obj["tagType"].toString();
        if (typeStr == "PID") tag.tagType = TagType::PID;
        else if (typeStr == "AO") tag.tagType = TagType::AO;
        else if (typeStr == "DI") tag.tagType = TagType::DI;
        else if (typeStr == "DO") tag.tagType = TagType::DO;
        else tag.tagType = TagType::AI;
        tag.engHigh = static_cast<float>(obj["engHigh"].toDouble(100.0));
        tag.engLow = static_cast<float>(obj["engLow"].toDouble(0.0));
        tag.highHighLimit = static_cast<float>(obj["highHighLimit"].toDouble(90.0));
        tag.highLimit = static_cast<float>(obj["highLimit"].toDouble(80.0));
        tag.lowLimit = static_cast<float>(obj["lowLimit"].toDouble(10.0));
        tag.lowLowLimit = static_cast<float>(obj["lowLowLimit"].toDouble(5.0));
        tag.deadband = static_cast<float>(obj["deadband"].toDouble(1.0));
        tag.modbusServerAddr = obj["modbusServerAddr"].toInt(1);
        tag.modbusRegAddr = obj["modbusRegAddr"].toInt(0);
        tag.modbusRegCount = obj["modbusRegCount"].toInt(1);
        tags.append(tag);
    }
    clear();
    addTags(tags);
    return true;
}

bool TagManager::saveToJson(const QString& jsonPath) const {
    QReadLocker lock(&m_rwlock);
    QJsonArray tagsArray;
    for (const TagInfo& tag : m_tags) {
        QJsonObject obj;
        obj["tagId"] = static_cast<int>(tag.tagId);
        obj["tagName"] = tag.tagName;
        obj["description"] = tag.description;
        obj["unit"] = tag.unit;
        obj["tagType"] = static_cast<int>(tag.tagType);
        obj["engHigh"] = tag.engHigh;
        obj["engLow"] = tag.engLow;
        obj["highHighLimit"] = tag.highHighLimit;
        obj["highLimit"] = tag.highLimit;
        obj["lowLimit"] = tag.lowLimit;
        obj["lowLowLimit"] = tag.lowLowLimit;
        obj["deadband"] = tag.deadband;
        obj["modbusServerAddr"] = tag.modbusServerAddr;
        obj["modbusRegAddr"] = tag.modbusRegAddr;
        obj["modbusRegCount"] = tag.modbusRegCount;
        tagsArray.append(obj);
    }
    QJsonDocument doc(tagsArray);
    QFile file(jsonPath);
    if (!file.open(QIODevice::WriteOnly)) return false;
    file.write(doc.toJson(QJsonDocument::Indented));
    file.close();
    return true;
}
