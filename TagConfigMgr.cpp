#include "TagConfigMgr.h"
#include "logger.h"
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
TagConfigMgr& TagConfigMgr::instance()
{
	static TagConfigMgr instance;
	return instance;
}

bool TagConfigMgr::addTag(const TagInfo& tag)
{
	QWriteLocker lock(&m_rwlock);
	// 检查是否已存在
	if (m_tags.contains(tag.tagId))
	{
		LOG_WARN("TagConfigMgr", QString("位号已存在: %1 (ID=%2)")
			.arg(tag.tagName).arg(tag.tagId));
		return false;
	}
	m_tags.insert(tag.tagId,tag);

	m_nameIndex.insert(tag.tagName,tag.tagId);

	quint32 modbusKey = (static_cast<quint32>(tag.modbusServerAddr) << 16) | static_cast<quint32>(tag.modbusRegAddr);
	m_modbusAddrIndex.insert(modbusKey,tag.tagId);
	// 添加设备索引（假设tagId的高8位是设备ID）
	int deviceId = tag.tagId >> 24;
	m_deviceIndex[deviceId].append(tag.tagId);
	LOG_INFO("TagConfigMgr", QString("添加位号: %1 (%2) ID=%3 单位=%4")
		.arg(tag.tagName).arg(tag.description)
		.arg(tag.tagId).arg(tag.unit));

	emit tagAdded(tag.tagId);
	return true;

}

bool TagConfigMgr::removeTag(quint32 tagId)
{
	QWriteLocker lock(&m_rwlock);
	auto it = m_tags.find(tagId);
	if (it == m_tags.end()) {
		return false;
	}

	QString name = it->tagName;
	quint32 modbusKey = (static_cast<quint32>(it->modbusServerAddr) << 16) |
		static_cast<quint32>(it->modbusRegAddr);
	int deviceId = tagId >> 24;

	m_nameIndex.remove(name);
	m_modbusAddrIndex.remove(modbusKey);

	// 移除设备索引
	if (m_deviceIndex.contains(deviceId)) {
		m_deviceIndex[deviceId].removeOne(tagId);
		if (m_deviceIndex[deviceId].isEmpty()) {
			m_deviceIndex.remove(deviceId);
		}
	}
	m_tags.erase(it);

	LOG_INFO("TagConfigMgr", QString("移除位号: %1 (ID=%2)").arg(name).arg(tagId));

	emit tagRemoved(tagId);
	return true;

}

TagInfo TagConfigMgr::getTag(quint32 tagId) const
{
	QReadLocker locker(&m_rwlock);
	return m_tags.value(tagId);
}

TagInfo TagConfigMgr::getTagByName(const QString& tagName) const
{
	QReadLocker locker(&m_rwlock);
	quint32 id = m_nameIndex.value(tagName, 0);
	return m_tags.value(id);
}

QList<TagInfo> TagConfigMgr::getAllTags() const
{
	QReadLocker locker(&m_rwlock);
	return m_tags.values();
}

QStringList TagConfigMgr::getAllTagNames() const
{
	QReadLocker locker(&m_rwlock);
	return m_nameIndex.keys();
}

int TagConfigMgr::tagCount() const
{
	QReadLocker locker(&m_rwlock);
	return m_tags.size();
}

QPair<float, float> TagConfigMgr::getRange(quint32 tagId) const
{
	QReadLocker locker(&m_rwlock);
	auto it = m_tags.constFind(tagId);
	if (it != m_tags.constEnd()) {
		return { it->engLow, it->engHigh };
	}
	return { 0.0f, 100.0f };
}

QString TagConfigMgr::getUxnit(quint32 tagId) const
{
	QReadLocker locker(&m_rwlock);
	return m_tags.value(tagId).unit;
}

TagConfigMgr::AlarmLimits TagConfigMgr::getAlarmLimits(quint32 tagId) const
{
	QReadLocker locker(&m_rwlock);
	auto it = m_tags.constFind(tagId);
	if (it != m_tags.constEnd()) {
		AlarmLimits limits;
		limits.highHigh = it->highHighLimit;
		limits.high = it->highLimit;
		limits.low = it->lowLimit;
		limits.lowLow = it->lowLowLimit;
		limits.deadband = it->deadband;
		return limits;
	}
	return {};
}

TagConfigMgr::ModbusMapping TagConfigMgr::getModbusMapping(quint32 tagId) const
{
	QReadLocker locker(&m_rwlock);
	auto it = m_tags.constFind(tagId);
	if (it != m_tags.constEnd()) {
		TagConfigMgr::ModbusMapping mapping;
		mapping.serverAddr = it->modbusServerAddr;
		mapping.regAddr = it->modbusRegAddr;
		mapping.regCount = it->modbusRegCount;
		return mapping;
	}
	return {};
}

quint32 TagConfigMgr::findTagByModbusAddr(int serverAddr, int regAddr) const
{
	QReadLocker locker(&m_rwlock);
	quint32 modbusKey = (static_cast<quint32>(serverAddr) << 16) |
		static_cast<quint32>(regAddr);
	return m_modbusAddrIndex.value(modbusKey, 0);
}

bool TagConfigMgr::updateAlarmLimits(quint32 tagId, const AlarmLimits& limits)
{
	QWriteLocker locker(&m_rwlock);
	auto it = m_tags.find(tagId);
	if (it == m_tags.end()) {
		return false;
	}

	it->highHighLimit = limits.highHigh;
	it->highLimit = limits.high;
	it->lowLimit = limits.low;
	it->lowLowLimit = limits.lowLow;
	it->deadband = limits.deadband;

	LOG_INFO("TagConfigMgr", QString("更新报警限值: %1 HH=%2 H=%3 L=%4 LL=%5")
		.arg(it->tagName).arg(limits.highHigh).arg(limits.high)
		.arg(limits.low).arg(limits.lowLow));

	emit configChanged(tagId);
	return true;
}

bool TagConfigMgr::updateRange(quint32 tagId, float engLow, float engHigh)
{
	QWriteLocker locker(&m_rwlock);
	auto it = m_tags.find(tagId);
	if (it == m_tags.end()) {
		return false;
	}

	it->engLow = engLow;
	it->engHigh = engHigh;

	LOG_INFO("TagConfigMgr", QString("更新量程: %1 范围=%2~%3")
		.arg(it->tagName).arg(engLow).arg(engHigh));

	emit configChanged(tagId);
	return true;
}

void TagConfigMgr::addTags(const QVector<TagInfo>& tags)
{
	for (const auto& tag : tags) {
		addTag(tag);
	}
	LOG_INFO("TagConfigMgr", QString("批量添加位号完成，共%1个").arg(tags.size()));
}

void TagConfigMgr::clear()
{
	QWriteLocker locker(&m_rwlock);
	m_tags.clear();
	m_nameIndex.clear();
	m_modbusAddrIndex.clear();
	m_deviceIndex.clear();
	LOG_INFO("TagConfigMgr", "已清空所有位号配置");
}

QVector<quint32> TagConfigMgr::getTagsByDevice(int deviceId) const
{
	QReadLocker locker(&m_rwlock);
	return m_deviceIndex.value(deviceId);
}

bool TagConfigMgr::loadFromJson(const QString& jsonPath)
{
	QFile file(jsonPath);
	if (!file.open(QIODevice::ReadOnly)) {
		LOG_ERROR("TagConfigMgr", QString("无法打开配置文件: %1").arg(jsonPath));
		return false;
	}

	QByteArray data = file.readAll();
	file.close();

	QJsonParseError error;
	QJsonDocument doc = QJsonDocument::fromJson(data, &error);
	if (error.error != QJsonParseError::NoError) {
		LOG_ERROR("TagConfigMgr", QString("JSON解析失败: %1").arg(error.errorString()));
		return false;
	}

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
		tag.tagType = static_cast<TagType>(obj["tagType"].toInt());
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

	LOG_INFO("TagConfigMgr", QString("从JSON加载配置完成: %1, 共%2个位号")
		.arg(jsonPath).arg(tags.size()));
	return true;
}

bool TagConfigMgr::saveToJson(const QString& jsonPath) const
{
	QReadLocker locker(&m_rwlock);

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
	if (!file.open(QIODevice::WriteOnly)) {
		LOG_ERROR("TagConfigMgr", QString("无法写入配置文件: %1").arg(jsonPath));
		return false;
	}

	file.write(doc.toJson(QJsonDocument::Indented));
	file.close();

	LOG_INFO("TagConfigMgr", QString("配置已保存到: %1, 共%2个位号")
		.arg(jsonPath).arg(m_tags.size()));
	return true;
}
