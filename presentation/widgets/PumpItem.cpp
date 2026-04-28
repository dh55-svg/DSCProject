#include "PumpItem.h"
#include <QtMath>

PumpItem::PumpItem(QGraphicsItem* parent)
    : BaseGraphicsItem(parent)
{
}

void PumpItem::loadFromJson(const QJsonObject& json) {
    BaseGraphicsItem::loadFromJson(json);
    m_size = json.value("size").toDouble(30.0);
}

void PumpItem::setSize(qreal size) {
    m_size = size;
    prepareGeometryChange();
}

QRectF PumpItem::boundingRect() const {
    return QRectF(-m_size, -m_size, m_size * 2, m_size * 2);
}

void PumpItem::paint(QPainter* painter, const QStyleOptionGraphicsItem*, QWidget*) {
    painter->setRenderHint(QPainter::Antialiasing);

    painter->setPen(QPen(m_bodyColor.darker(130), 2));
    painter->setBrush(m_bodyColor);
    painter->drawEllipse(QPointF(0, 0), m_size, m_size);

    painter->setPen(QPen(m_bodyColor.lighter(150), 1.5));
    for (int i = 0; i < 3; ++i) {
        qreal angle = m_rotationAngle + i * 120.0;
        qreal rad = qDegreesToRadians(angle);
        painter->drawLine(QPointF(0, 0),
                          QPointF(m_size * 0.7 * qCos(rad), m_size * 0.7 * qSin(rad)));
    }

    painter->setPen(QColor(220, 220, 220));
    painter->setFont(QFont("Arial", 9));
    painter->drawText(QRectF(-m_size, m_size + 5, m_size * 2, 20),
                      Qt::AlignHCenter, m_running ? "RUN" : "STOP");
}

void PumpItem::updateAppearance() {
    if (m_quality == DataQuality::Bad) {
        m_bodyColor = QColor(100, 100, 100);
        m_running = false;
    } else {
        m_running = (m_tagValue > 0.5f);
        if (m_running) {
            m_bodyColor = alarmColor();
            m_rotationAngle += 5.0;
            if (m_rotationAngle >= 360.0) m_rotationAngle -= 360.0;
        } else {
            m_bodyColor = QColor(120, 120, 140);
        }
    }
    update();
}
