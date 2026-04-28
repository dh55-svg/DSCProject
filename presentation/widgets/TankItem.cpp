#include "TankItem.h"

TankItem::TankItem(QGraphicsItem* parent)
    : BaseGraphicsItem(parent)
{
}

void TankItem::loadFromJson(const QJsonObject& json) {
    BaseGraphicsItem::loadFromJson(json);
    m_width = json.value("width").toDouble(60.0);
    m_height = json.value("height").toDouble(100.0);
}

void TankItem::setSize(qreal width, qreal height) {
    m_width = width;
    m_height = height;
    prepareGeometryChange();
}

QRectF TankItem::boundingRect() const {
    return QRectF(-m_width / 2 - 20, -m_height / 2 - 10, m_width + 40, m_height + 30);
}

void TankItem::paint(QPainter* painter, const QStyleOptionGraphicsItem*, QWidget*) {
    painter->setRenderHint(QPainter::Antialiasing);

    QRectF tankRect(-m_width / 2, -m_height / 2, m_width, m_height);
    painter->setPen(QPen(m_tankColor, 2));
    painter->setBrush(QColor(30, 30, 50));
    painter->drawRoundedRect(tankRect, 4, 4);

    qreal liquidHeight = m_height * (m_levelPercent / 100.0);
    QRectF liquidRect(-m_width / 2 + 2, m_height / 2 - liquidHeight, m_width - 4, liquidHeight);
    painter->setPen(Qt::NoPen);
    painter->setBrush(m_liquidColor);
    painter->drawRect(liquidRect);

    painter->setPen(QColor(220, 220, 220));
    painter->setFont(QFont("Arial", 9));
    QString text = QString("%1%\n%2").arg(m_levelPercent, 0, 'f', 1).arg(qualityText());
    painter->drawText(tankRect.adjusted(0, -30, 0, 0), Qt::AlignHCenter | Qt::AlignBottom, text);
}

void TankItem::updateAppearance() {
    if (m_quality == DataQuality::Bad) {
        m_liquidColor = QColor(100, 100, 100);
    } else {
        m_liquidColor = alarmColor();
    }
    update();
}
