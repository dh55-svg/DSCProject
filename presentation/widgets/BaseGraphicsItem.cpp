#include "BaseGraphicsItem.h"
#include <QGraphicsSceneMouseEvent>

BaseGraphicsItem::BaseGraphicsItem(QGraphicsItem* parent)
    : QGraphicsObject(parent)
{
    setFlags(QGraphicsItem::ItemIsSelectable | QGraphicsItem::ItemIsFocusable);
}

void BaseGraphicsItem::bindTag(const QString& tagName) {
    m_tagName = tagName;
}

void BaseGraphicsItem::loadFromJson(const QJsonObject& json) {
    m_tagName = json.value("tagName").toString();
}

void BaseGraphicsItem::onTagValueChanged(float newValue, AlarmLimit alarmState, DataQuality quality) {
    m_tagValue = newValue;
    m_alarmState = alarmState;
    m_quality = quality;
    updateAppearance();
}

QColor BaseGraphicsItem::alarmColor() const {
    switch (m_alarmState) {
    case AlarmLimit::HighHigh: return QColor(255, 60, 60);
    case AlarmLimit::High:     return QColor(255, 140, 0);
    case AlarmLimit::Low:      return QColor(255, 200, 0);
    case AlarmLimit::LowLow:   return QColor(255, 60, 60);
    case AlarmLimit::Deviation: return QColor(200, 0, 200);
    default:                   return QColor(60, 180, 60);
    }
}

QString BaseGraphicsItem::qualityText() const {
    switch (m_quality) {
    case DataQuality::Good:      return "OK";
    case DataQuality::Bad:       return "BAD";
    case DataQuality::Uncertain: return "UNC";
    }
    return "???";
}

void BaseGraphicsItem::mousePressEvent(QGraphicsSceneMouseEvent* event) {
    Q_UNUSED(event);
    emit itemClicked(this);
}
