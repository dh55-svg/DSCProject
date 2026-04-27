#include "ValveItem.h"
#include <QTime>

ValveItem::ValveItem(QGraphicsItem* parent)
    : BaseGraphicsItem(parent) {
    m_openColor = QColor(0, 200, 0);
    m_closedColor = QColor(255, 0, 0);
    m_currentColor = m_openColor;


    setFlags(ItemIsSelectable | ItemIsFocusable);
    setAcceptHoverEvents(true);
}

void ValveItem::loadFromJson(const QJsonObject& json) {
    BaseGraphicsItem::loadFromJson(json);

    m_size = json["size"].toDouble(30.0);
    m_bindProperty = json["bind_property"].toString("outputValue");

    if (json.contains("color_open")) {
        m_openColor = QColor(json["color_open"].toString());
    }
    if (json.contains("color_closed")) {
        m_closedColor = QColor(json["color_closed"].toString());
    }

    updateAppearance();
}

void ValveItem::setSize(qreal size) {
    m_size = size;
    update();
}

void ValveItem::setOpenColor(const QColor& color) {
    m_openColor = color;
    update();
}

void ValveItem::setClosedColor(const QColor& color) {
    m_closedColor = color;
    update();
}

QRectF ValveItem::boundingRect() const {
    return QRectF(-m_size, -m_size, m_size * 2, m_size * 2);
}

void ValveItem::paint(QPainter* painter, const QStyleOptionGraphicsItem* option,
    QWidget* widget) {
    Q_UNUSED(option);
    Q_UNUSED(widget);

    painter->setRenderHint(QPainter::Antialiasing, true);



    qreal halfSize = m_size * 0.8;
    qreal gap = m_size * 0.1 * (1.0 - m_openPercent / 100.0);

    QPen pen(m_currentColor.darker(150), 2);
    painter->setPen(pen);
    painter->setBrush(m_currentColor);


    QPainterPath leftTriangle;
    leftTriangle.moveTo(-halfSize, -halfSize);
    leftTriangle.lineTo(-gap, 0);
    leftTriangle.lineTo(-halfSize, halfSize);
    leftTriangle.closeSubpath();
    painter->drawPath(leftTriangle);


    QPainterPath rightTriangle;
    rightTriangle.moveTo(halfSize, -halfSize);
    rightTriangle.lineTo(gap, 0);
    rightTriangle.lineTo(halfSize, halfSize);
    rightTriangle.closeSubpath();
    painter->drawPath(rightTriangle);


    painter->setPen(Qt::white);
    QFont font = painter->font();
    font.setPointSize(8);
    painter->setFont(font);
    painter->drawText(boundingRect(), Qt::AlignTop | Qt::AlignHCenter,
        m_tagName);


    if (tagQuality() == DataQuality::Bad) {
        painter->setPen(QColor(255, 255, 0));
        font.setPointSize(10);
        painter->setFont(font);
        painter->drawText(boundingRect(), Qt::AlignCenter, "???");
    }
    else {

        painter->setPen(Qt::white);
        font.setPointSize(9);
        painter->setFont(font);
        painter->drawText(boundingRect(), Qt::AlignCenter,
            QString("%1%").arg(m_openPercent, 0, 'f', 0));
    }


    if (m_alarmState == AlarmLimit::HighHigh || m_alarmState == AlarmLimit::LowLow) {

        QTime now = QTime::currentTime();
        if (now.msec() < 500) {
            painter->setPen(QPen(QColor(255, 0, 0), 3));
            painter->setBrush(Qt::NoBrush);
            painter->drawRect(boundingRect().adjusted(-2, -2, 2, 2));
        }
    }
}

void ValveItem::updateAppearance() {

    TagInfo tag = TagConfigMgr::instance().getTag(m_tagId);
    if (tag.tagId != 0) {
        if (m_bindProperty == "outputValue") {
            m_openPercent = tag.out();
        }
        else {
            m_openPercent = tag.pv();
        }
    }


    float ratio = m_openPercent / 100.0f;
    ratio = qBound(0.0f, ratio, 1.0f);
    m_currentColor = QColor(
        static_cast<int>(m_closedColor.red() * (1 - ratio) + m_openColor.red() * ratio),
        static_cast<int>(m_closedColor.green() * (1 - ratio) + m_openColor.green() * ratio),
        static_cast<int>(m_closedColor.blue() * (1 - ratio) + m_openColor.blue() * ratio)
    );


    if (m_alarmState != AlarmLimit::Normal) {
        m_currentColor = alarmColor();
    }

    update();
}
