#include "PipeItem.h"

QTimer* PipeItem::s_animTimer = nullptr;
qreal PipeItem::s_globalOffset = 0.0;
int PipeItem::s_pipeCount = 0;

PipeItem::PipeItem(QGraphicsItem* parent)
    : BaseGraphicsItem(parent) {
    m_pipeColor = QColor(120, 120, 120);
    m_fluidColor = QColor(0, 150, 255);

    initAnimation();
    s_pipeCount++;
}

void PipeItem::initAnimation() {


    if (!s_animTimer) {
        s_animTimer = new QTimer();
        s_animTimer->setInterval(80);
        QObject::connect(s_animTimer, &QTimer::timeout, []() {
            s_globalOffset += 3.0;
            if (s_globalOffset > 20.0) {
                s_globalOffset -= 20.0;
            }
            });
        s_animTimer->start();
    }
}

void PipeItem::loadFromJson(const QJsonObject& json) {
    BaseGraphicsItem::loadFromJson(json);

    m_length = json["length"].toDouble(100.0);
    m_width = json["width"].toDouble(8.0);
    m_flowing = json["flowing"].toBool(true);

    int dir = json["flowDirection"].toInt(0);
    m_flowDir = static_cast<FlowDirection>(dir);


    if (json.contains("pipeColor")) {
        m_pipeColor = QColor(json["pipeColor"].toString());
    }
    if (json.contains("fluidColor")) {
        m_fluidColor = QColor(json["fluidColor"].toString());
    }

    updateAppearance();
}

void PipeItem::setLength(qreal length) {
    m_length = length;
    update();
}

void PipeItem::setWidth(qreal width) {
    m_width = width;
    update();
}

void PipeItem::setFlowDirection(FlowDirection dir) {
    m_flowDir = dir;
    update();
}

void PipeItem::setFlowing(bool flowing) {
    m_flowing = flowing;
    update();
}

QRectF PipeItem::boundingRect() const {

    if (m_flowDir == LeftToRight || m_flowDir == RightToLeft) {
        return QRectF(0, -m_width / 2, m_length, m_width);
    }
    else {
        return QRectF(-m_width / 2, 0, m_width, m_length);
    }
}

void PipeItem::paint(QPainter* painter, const QStyleOptionGraphicsItem* option,
    QWidget* widget) {
    Q_UNUSED(option);
    Q_UNUSED(widget);

    painter->setRenderHint(QPainter::Antialiasing, true);

    QRectF rect = boundingRect();


    painter->setPen(Qt::NoPen);
    painter->setBrush(m_pipeColor);
    painter->drawRoundedRect(rect, m_width / 4, m_width / 4);


    if (m_flowing) {
        painter->setBrush(m_fluidColor);
        painter->setClipRect(rect.adjusted(1, 1, -1, -1));


        QPen flowPen(m_fluidColor, m_width * 0.6);
        flowPen.setStyle(Qt::DashLine);
        flowPen.setDashOffset(s_globalOffset * (m_flowDir == RightToLeft ||
            m_flowDir == BottomToTop ? -1 : 1));
        flowPen.setDashPattern({ 4, 6 });
        painter->setPen(flowPen);

        if (m_flowDir == LeftToRight || m_flowDir == RightToLeft) {
            painter->drawLine(QPointF(0, 0), QPointF(m_length, 0));
        }
        else {
            painter->drawLine(QPointF(0, 0), QPointF(0, m_length));
        }
    }


    if (tagQuality() == DataQuality::Bad) {
        painter->setClipRect(rect);
        QPen crossPen(QColor(255, 0, 0, 150), 2);
        painter->setPen(crossPen);
        painter->drawLine(rect.topLeft(), rect.bottomRight());
        painter->drawLine(rect.topRight(), rect.bottomLeft());
    }
}

void PipeItem::updateAppearance() {

    if (m_alarmState != AlarmLimit::Normal) {
        m_fluidColor = alarmColor();
    }
    else {
        m_fluidColor = QColor(0, 150, 255);
    }


    if (m_quality == DataQuality::Bad) {
        m_flowing = false;
    }

    update();
}
