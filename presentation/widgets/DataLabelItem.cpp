#include "DataLabelItem.h"

DataLabelItem::DataLabelItem(QGraphicsItem* parent)
    : BaseGraphicsItem(parent)
{
}

void DataLabelItem::loadFromJson(const QJsonObject& json) {
    BaseGraphicsItem::loadFromJson(json);
    m_staticText = json.value("text").toString();
    m_fontSize = json.value("fontSize").toInt(12);
    m_showValue = json.value("showValue").toBool(true);
    m_showAlarm = json.value("showAlarm").toBool(false);
    m_prefix = json.value("prefix").toString();
    m_suffix = json.value("suffix").toString();
}

void DataLabelItem::setText(const QString& text) { m_staticText = text; update(); }
void DataLabelItem::setFontSize(int size) { m_fontSize = size; update(); }
void DataLabelItem::setTextColor(const QColor& color) { m_textColor = color; update(); }

QRectF DataLabelItem::boundingRect() const {
    return QRectF(0, 0, 120, m_fontSize + 8);
}

void DataLabelItem::paint(QPainter* painter, const QStyleOptionGraphicsItem*, QWidget*) {
    painter->setPen(m_textColor);
    painter->setFont(QFont("Consolas", m_fontSize));
    painter->drawText(boundingRect(), Qt::AlignLeft | Qt::AlignVCenter, m_displayText);
}

void DataLabelItem::updateAppearance() {
    QStringList parts;
    if (!m_prefix.isEmpty()) parts << m_prefix;
    if (m_showValue) parts << QString::number(m_tagValue, 'f', 1);
    if (!m_staticText.isEmpty()) parts << m_staticText;
    if (!m_suffix.isEmpty()) parts << m_suffix;
    if (m_showAlarm && m_alarmState != AlarmLimit::Normal) {
        switch (m_alarmState) {
        case AlarmLimit::HighHigh: parts << "[HH]"; break;
        case AlarmLimit::High:     parts << "[HI]"; break;
        case AlarmLimit::Low:      parts << "[LO]"; break;
        case AlarmLimit::LowLow:   parts << "[LL]"; break;
        default: break;
        }
    }
    m_displayText = parts.join(" ");
    m_textColor = (m_quality == DataQuality::Bad) ? QColor(100, 100, 100) : alarmColor();
    update();
}
