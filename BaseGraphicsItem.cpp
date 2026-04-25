#include "BaseGraphicsItem.h"
#include "logger.h"
#include <QGraphicsSceneMouseEvent>
BaseGraphicsItem::BaseGraphicsItem(QGraphicsItem* parent):QGraphicsObject(parent)
{
	// 开启图元缓存，减少重绘开销
	// 工业画面上可能有几百个图元，不缓存的话CPU扛不住
	setCacheMode(DeviceCoordinateCache);
}
void BaseGraphicsItem::bindTag(const QString& tagName)
{
	m_tagName = tagName;
	// 从RealtimeDb查找位号ID
	TagInfo tag = RealtimeDb::instance().getTagByName(m_tagName);
	if (tag.tagId == 0)
	{
		LOG_WARN("Graphics", QString("位号不存在: %1").arg(tagName));
		return;
	}

	m_tagId = tag.tagId;
	m_tagValue = tag.pv();
	m_alarmState = tag.alarm();
	m_quality = tag.qual();

	// 注册数据变更回调
	// 当位号值变化时，RealtimeDb会调用onTagValueChanged
	RealtimeDb::instance().registerCallback(m_tagId,
		[this](quint32 tagId, float newValue) {
			QMetaObject::invokeMethod(this, [this, tagId, newValue]() {
				onTagValueChanged(tagId, newValue);
				}, Qt::QueuedConnection);
		});

	LOG_INFO("Graphics", QString("图元绑定位号: %1 (ID=%2)")
		.arg(tagName).arg(m_tagId));
}

void BaseGraphicsItem::loadFromJson(const QJsonObject& json)
{
	if (json.contains("binding_tag"))
	{
		bindTag(json["binding_tag"].toString());
	}
	// 位置和大小
	if (json.contains("x")) {
		setX(json["x"].toDouble());
	}
	if (json.contains("y")) {
		setY(json["y"].toDouble());
	}
	if (json.contains("rotation")) {
		setRotation(json["rotation"].toDouble());
	}
}

void BaseGraphicsItem::onTagValueChanged(quint32 tagId, float newValue)
{
	if (tagId != m_tagId) {
		return;
	}
	m_tagValue = newValue;
	// 同步更新报警状态和质量码
	TagInfo tag = RealtimeDb::instance().getTag(tagId);
	if (tag.tagId != 0) {
		m_alarmState = tag.alarm();
		m_quality = tag.qual();
	}

	// 调用子类的更新方法
	updateAppearance();
	update(); // 触发重绘

}

QColor BaseGraphicsItem::alarmColor() const
{
	switch (m_alarmState) {
	case AlarmLimit::HighHigh:
	case AlarmLimit::LowLow:
		return QColor(255, 0, 0);       // 红色 - 紧急报警
	case AlarmLimit::High:
	case AlarmLimit::Low:
		return QColor(255, 165, 0);     // 橙色 - 预警
	case AlarmLimit::Normal:
	default:
		return QColor(0, 200, 0);       // 绿色 - 正常
	}
}

QString BaseGraphicsItem::qualityText() const
{
	switch (m_quality) {
	case DataQuality::Good:
		return QString();               // 好数据不显示额外标记
	case DataQuality::Bad:
		return "???";                   // 坏数据显示问号
	case DataQuality::Uncertain:
		return "?";                     // 存疑数据显示单问号
	default:
		return "???";
	}
}

void BaseGraphicsItem::mousePressEvent(QGraphicsSceneMouseEvent* event)
{
	Q_UNUSED(event);
	// 点击图元时发射信号，UI层接收后弹出操作面板
	emit itemClicked(this);
}
