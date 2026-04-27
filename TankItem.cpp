#include "TankItem.h"
#include <QTime>

TankItem::TankItem(QGraphicsItem* parent)
    : BaseGraphicsItem(parent) {
    m_liquidColor = QColor(0, 120, 255, 180);  // 蓝色半透明液体
    m_tankColor = QColor(80, 80, 80);           // 深灰色罐体
}

void TankItem::loadFromJson(const QJsonObject& json) {
    BaseGraphicsItem::loadFromJson(json);

    m_width = json["width"].toDouble(60.0);
    m_height = json["height"].toDouble(100.0);

    if (json.contains("liquidColor")) {
        m_liquidColor = QColor(json["liquidColor"].toString());
    }
    if (json.contains("tankColor")) {
        m_tankColor = QColor(json["tankColor"].toString());
    }

    updateAppearance();
}

void TankItem::setSize(qreal width, qreal height) {
    m_width = width;
    m_height = height;
    update();
}

QRectF TankItem::boundingRect() const {
    // 留出上下标签空间
    return QRectF(-m_width / 2 - 5, -20, m_width + 10, m_height + 40);
}

void TankItem::paint(QPainter* painter, const QStyleOptionGraphicsItem* option,
    QWidget* widget) {
    Q_UNUSED(option);
    Q_UNUSED(widget);

    painter->setRenderHint(QPainter::Antialiasing, true);

    QRectF tankRect(-m_width / 2, 0, m_width, m_height);

    // 1. 绘制罐体外壳
    painter->setPen(QPen(m_tankColor, 2));
    painter->setBrush(QColor(30, 30, 30)); // 罐体内部深色背景
    painter->drawRoundedRect(tankRect, 5, 5);

    // 2. 绘制液位
    if (m_levelPercent > 0.0f) {
        // 计算液位高度
        qreal liquidHeight = m_height * (m_levelPercent / 100.0f);
        qreal liquidTop = m_height - liquidHeight;

        QRectF liquidRect(-m_width / 2 + 2, liquidTop,
            m_width - 4, liquidHeight - 2);

        // 裁剪到罐体内部
        painter->save();
        painter->setClipRect(tankRect.adjusted(2, 2, -2, -2));

        // 绘制液体填充
        painter->setPen(Qt::NoPen);
        painter->setBrush(m_liquidColor);
        painter->drawRect(liquidRect);

        // 绘制液面波浪效果
        QPainterPath wavePath;
        qreal waveAmplitude = 2.0; // 波浪振幅
        qreal waveY = liquidTop;

        wavePath.moveTo(liquidRect.left(), waveY);
        for (qreal x = liquidRect.left(); x <= liquidRect.right(); x += 2) {
            qreal y = waveY + waveAmplitude *
                qSin((x + m_waveOffset) * 0.15);
            wavePath.lineTo(x, y);
        }
        wavePath.lineTo(liquidRect.right(), liquidRect.bottom());
        wavePath.lineTo(liquidRect.left(), liquidRect.bottom());
        wavePath.closeSubpath();

        QColor waveColor = m_liquidColor.lighter(120);
        waveColor.setAlpha(100);
        painter->setBrush(waveColor);
        painter->drawPath(wavePath);

        painter->restore();
    }

    // 3. 绘制刻度线
    painter->setPen(QPen(QColor(200, 200, 200), 1));
    QFont font = painter->font();
    font.setPointSize(6);
    painter->setFont(font);
    for (int i = 0; i <= 4; ++i) {
        qreal y = m_height * i / 4;
        painter->drawLine(QPointF(m_width / 2, y),
            QPointF(m_width / 2 + 5, y));
        int percent = 100 - i * 25;
        painter->drawText(QPointF(m_width / 2 + 8, y + 4),
            QString("%1%").arg(percent));
    }

    // 4. 绘制位号名和数值
    painter->setPen(Qt::white);
    font.setPointSize(8);
    font.setBold(true);
    painter->setFont(font);

    // 位号名在上方
    painter->drawText(QRectF(-m_width / 2, -18, m_width, 16),
        Qt::AlignCenter, m_tagName);

    // 数值在液位旁边
    font.setBold(false);
    font.setPointSize(9);
    painter->setFont(font);

    if (tagQuality() == DataQuality::Bad) {
        painter->setPen(QColor(255, 255, 0));
        painter->drawText(QRectF(-m_width / 2 - 40, m_height / 2 - 8, 35, 16),
            Qt::AlignRight | Qt::AlignVCenter, "???");
    }
    else {
        painter->setPen(Qt::white);
        painter->drawText(QRectF(-m_width / 2 - 50, m_height / 2 - 8, 45, 16),
            Qt::AlignRight | Qt::AlignVCenter,
            QString("%1%").arg(m_levelPercent, 0, 'f', 1));
    }

    // 5. 报警高亮边框
    if (m_alarmState != AlarmLimit::Normal) {
        QPen alarmPen(alarmColor(), 3);
        painter->setPen(alarmPen);
        painter->setBrush(Qt::NoBrush);
        painter->drawRoundedRect(tankRect.adjusted(-3, -3, 3, 3), 6, 6);
    }

    // 更新波浪动画偏移
    m_waveOffset += 0.5;
}

void TankItem::updateAppearance() {
    // 根据绑定位号的当前值计算液位百分比
    TagInfo tag = TagConfigMgr::instance().getTag(m_tagId);
    if (tag.tagId != 0) {
        float range = tag.engHigh - tag.engLow;
        if (range > 0.0f) {
            m_levelPercent = (m_tagValue - tag.engLow) / range * 100.0f;
            m_levelPercent = qBound(0.0f, m_levelPercent, 100.0f);
        }
    }

    // 根据报警状态改变液体颜色
    if (m_alarmState == AlarmLimit::HighHigh || m_alarmState == AlarmLimit::High) {
        m_liquidColor = QColor(255, 80, 80, 180);   // 红色
    }
    else if (m_alarmState == AlarmLimit::LowLow || m_alarmState == AlarmLimit::Low) {
        m_liquidColor = QColor(255, 165, 0, 180);   // 橙色
    }
    else {
        m_liquidColor = QColor(0, 120, 255, 180);   // 正常蓝色
    }

    update();
}
