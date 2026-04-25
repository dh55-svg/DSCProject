#pragma once


#include "export.h"
#include "BaseGraphicsItem.h"
#include <QPainter>
#include <QPainterPath>

/**
 * @brief 动态液位图元
 *
 * 化工P&ID图上的反应釜/储罐液位显示。
 *
 * 实现原理：
 * - 罐体用圆角矩形表示
 * - 液位高度根据绑定位号的当前值按比例绘制
 * - 液面有波浪动画效果
 * - 液位颜色根据报警状态变化
 *
 * 踩坑经验：
 * - 液位图元是DCS画面上最直观的图元，操作员一眼就能看出罐有多满
 * - 液位动画不能太花哨，波浪效果1-2个周期就够了
 * - 液位数值必须显示在图元旁边，不能只靠颜色判断
 */
class GRAPHICS_EXPORT TankItem : public BaseGraphicsItem {
    Q_OBJECT

public:
    explicit TankItem(QGraphicsItem* parent = nullptr);

    QString itemTypeName() const override { return "Tank"; }
    void loadFromJson(const QJsonObject& json) override;

    void setSize(qreal width, qreal height);

    QRectF boundingRect() const override;
    void paint(QPainter* painter, const QStyleOptionGraphicsItem* option,
        QWidget* widget) override;

protected:
    void updateAppearance() override;

private:
    qreal m_width = 60.0;          // 罐体宽度
    qreal m_height = 100.0;        // 罐体高度
    float m_levelPercent = 50.0f;  // 液位百分比（0~100）
    QColor m_liquidColor;          // 液体颜色
    QColor m_tankColor;            // 罐体颜色
    qreal m_waveOffset = 0.0;     // 波浪动画偏移
};
