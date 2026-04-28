#include "ValveItem.h"
#include <QtMath>

ValveItem::ValveItem(QGraphicsItem* parent)
    : BaseGraphicsItem(parent)
{
}

void ValveItem::loadFromJson(const QJsonObject& json) {
    BaseGraphicsItem::loadFromJson(json);
    m_size = json.value("size").toDouble(30.0);
}

void ValveItem::setSize(qreal size) {
    m_size = size;
    prepareGeometryChange();
}

QRectF ValveItem::boundingRect() const {
    return QRectF(-m_size, -m_size, m_size * 2, m_size * 2);
}

void ValveItem::paint(QPainter* painter, const QStyleOptionGraphicsItem*, QWidget*) {
    painter->setRenderHint(QPainter::Antialiasing);

    qreal angle = m_openPercent / 100.0 * 90.0;
    qreal rad = qDegreesToRadians(angle);

    painter->setPen(QPen(m_currentColor, 2.5));
    painter->setBrush(m_currentColor);

    QPointF center(0, 0);
    QPointF left(-m_size * 0.7, 0);
    QPointF right(m_size * 0.7, 0);

    painter->drawLine(left, center);
    painter->drawLine(right, center);

    QPointF t1(-m_size * 0.5 * qCos(rad), -m_size * 0.5 * qSin(rad));
    QPointF t2(m_size * 0.5 * qCos(rad), m_size * 0.5 * qSin(rad));
    QPolygonF tri1;
    tri1 << center << t1 << QPointF(t1.x(), 0);
    QPolygonF tri2;
    tri2 << center << t2 << QPointF(t2.x(), 0);

    painter->drawPolygon(tri1);
    painter->drawPolygon(tri2);

    painter->setPen(QColor(220, 220, 220));
    painter->setFont(QFont("Arial", 8));
    QString text = QString("%1%").arg(m_openPercent, 0, 'f', 0);
    painter->drawText(QRectF(-m_size, m_size * 0.3, m_size * 2, 20), Qt::AlignHCenter, text);
}

void ValveItem::updateAppearance() {
    if (m_quality == DataQuality::Bad) {
        m_currentColor = QColor(100, 100, 100);
    } else {
        m_openPercent = qBound(0.0f, m_tagValue, 100.0f);
        qreal ratio = m_openPercent / 100.0;
        m_currentColor = QColor(
            int(m_closedColor.red() + (m_openColor.red() - m_closedColor.red()) * ratio),
            int(m_closedColor.green() + (m_openColor.green() - m_closedColor.green()) * ratio),
            int(m_closedColor.blue() + (m_openColor.blue() - m_closedColor.blue()) * ratio));
    }
    update();
}
