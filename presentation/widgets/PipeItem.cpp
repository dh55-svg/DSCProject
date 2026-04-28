#include "PipeItem.h"

QTimer* PipeItem::s_animTimer = nullptr;
qreal PipeItem::s_globalOffset = 0.0;
int PipeItem::s_pipeCount = 0;

PipeItem::PipeItem(QGraphicsItem* parent)
    : BaseGraphicsItem(parent)
{
    initAnimation();
}

void PipeItem::initAnimation() {
    if (!s_animTimer) {
        s_animTimer = new QTimer();
        QObject::connect(s_animTimer, &QTimer::timeout, []() {
            s_globalOffset += 2.0;
            if (s_globalOffset > 20.0) s_globalOffset -= 20.0;
        });
        s_animTimer->start(100);
    }
    ++s_pipeCount;
}

void PipeItem::loadFromJson(const QJsonObject& json) {
    BaseGraphicsItem::loadFromJson(json);
    m_length = json.value("length").toDouble(100.0);
    m_width = json.value("width").toDouble(8.0);
    m_flowDir = static_cast<FlowDirection>(json.value("flowDir").toInt(0));
}

void PipeItem::setLength(qreal length) { m_length = length; prepareGeometryChange(); }
void PipeItem::setWidth(qreal width) { m_width = width; prepareGeometryChange(); }
void PipeItem::setFlowDirection(FlowDirection dir) { m_flowDir = dir; update(); }
void PipeItem::setFlowing(bool flowing) { m_flowing = flowing; update(); }

QRectF PipeItem::boundingRect() const {
    bool horizontal = (m_flowDir == LeftToRight || m_flowDir == RightToLeft);
    if (horizontal)
        return QRectF(-m_width, -m_width, m_length + m_width * 2, m_width * 2);
    else
        return QRectF(-m_width, -m_width, m_width * 2, m_length + m_width * 2);
}

void PipeItem::paint(QPainter* painter, const QStyleOptionGraphicsItem*, QWidget*) {
    painter->setRenderHint(QPainter::Antialiasing);

    bool horizontal = (m_flowDir == LeftToRight || m_flowDir == RightToLeft);
    QRectF pipeRect;
    if (horizontal)
        pipeRect = QRectF(0, -m_width / 2, m_length, m_width);
    else
        pipeRect = QRectF(-m_width / 2, 0, m_width, m_length);

    painter->setPen(QPen(m_pipeColor.darker(120), 1));
    painter->setBrush(m_pipeColor);
    painter->drawRect(pipeRect);

    if (m_flowing) {
        m_animOffset = s_globalOffset;
        painter->setPen(QPen(m_fluidColor, m_width * 0.6));
        if (horizontal) {
            for (qreal x = m_animOffset; x < m_length; x += 20.0) {
                if (x > 0) painter->drawPoint(QPointF(x, m_width / 4));
            }
        } else {
            for (qreal y = m_animOffset; y < m_length; y += 20.0) {
                if (y > 0) painter->drawPoint(QPointF(m_width / 4, y));
            }
        }
    }
}

void PipeItem::updateAppearance() {
    if (m_quality == DataQuality::Bad) {
        m_pipeColor = QColor(100, 100, 100);
        m_flowing = false;
    } else {
        m_flowing = (m_tagValue > 0.01f);
        m_pipeColor = QColor(100, 100, 130);
    }
    update();
}
