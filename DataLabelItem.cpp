#include "DataLabelItem.h"
#include <QJsonObject>

DataLabelItem::DataLabelItem(QGraphicsItem* parent)
    : BaseGraphicsItem(parent)
{
}

void DataLabelItem::loadFromJson(const QJsonObject& json)
{
    BaseGraphicsItem::loadFromJson(json);

    m_staticText = json["text"].toString();
    m_displayText = m_staticText;
    m_fontSize = json["fontSize"].toInt(12);
    if (json.contains("textColor")) {
        m_textColor = QColor(json["textColor"].toString());
    }
    m_showValue = json["showValue"].toBool(true);
    m_showAlarm = json["showAlarm"].toBool(false);
    m_prefix = json["prefix"].toString();
    m_suffix = json["suffix"].toString();

    updateAppearance();
}

void DataLabelItem::setText(const QString& text)
{
    m_staticText = text;
    m_displayText = text;
    update();
}

void DataLabelItem::setFontSize(int size)
{
    m_fontSize = size;
    update();
}

void DataLabelItem::setTextColor(const QColor& color)
{
    m_textColor = color;
    update();
}

QRectF DataLabelItem::boundingRect() const
{
    // Estimate text width (~0.6 * fontSize per char) + padding
    qreal textWidth = m_displayText.length() * m_fontSize * 0.6 + 10;
    return QRectF(-5, -m_fontSize - 4, textWidth, m_fontSize + 12);
}

void DataLabelItem::paint(QPainter* painter, const QStyleOptionGraphicsItem* option,
    QWidget* widget)
{
    Q_UNUSED(option);
    Q_UNUSED(widget);

    painter->setRenderHint(QPainter::Antialiasing, true);

    QFont font = painter->font();
    font.setPointSize(m_fontSize);
    painter->setFont(font);

    // Draw alarm border if enabled and active
    if (m_showAlarm && m_alarmState != AlarmLimit::Normal) {
        painter->setPen(QPen(alarmColor(), 2));
        painter->setBrush(Qt::NoBrush);
        QRectF r = boundingRect();
        painter->drawRoundedRect(r.adjusted(-2, -2, 2, 2), 3, 3);
    }

    painter->setPen(m_textColor);
    painter->drawText(boundingRect(), Qt::AlignLeft | Qt::AlignVCenter, m_displayText);
}

void DataLabelItem::updateAppearance()
{
    if (m_staticText.isEmpty() && !m_tagName.isEmpty()) {
        m_displayText = m_tagName;
    } else {
        m_displayText = m_staticText;
    }

    if (m_showValue && m_tagId != 0) {
        m_displayText = m_prefix + m_displayText + " " + QString::number(m_tagValue, 'f', 1) + m_suffix;
    }

    // Alarm color
    if (m_showAlarm && m_alarmState != AlarmLimit::Normal) {
        m_textColor = alarmColor();
    }

    update();
}
