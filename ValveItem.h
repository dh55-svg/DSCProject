#pragma once
#include "export.h"
#include "BaseGraphicsItem.h"
#include <QPainter>
#include <QPainterPath>

/**
 * @brief 动态阀门图元
 *
 * 化工P&ID图上的阀门，需要表现开/关状态，并支持点击操作。
 *
 * 实现原理：
 * - 阀门用两个三角形（蝴蝶形）表示
 * - 开状态：绿色，三角形张开角度大
 * - 关状态：红色，三角形闭合
 * - 中间状态：根据绑定位号的outputValue(0~100%)插值颜色
 * - 点击弹出PID操作面板
 *
 * 踩坑经验：
 * - 阀门图元必须支持鼠标点击，但QGraphicsItem的事件处理
 *   要正确设置flags(ItemIsSelectable | ItemIsFocusable)
 * - 阀门状态变化时不能频繁new/delete图元，要复用
 */
class GRAPHICS_EXPORT ValveItem : public BaseGraphicsItem {
    Q_OBJECT

public:
    explicit ValveItem(QGraphicsItem* parent = nullptr);

    QString itemTypeName() const override { return "Valve"; }
    void loadFromJson(const QJsonObject& json) override;

    // 阀门属性
    void setSize(qreal size);
    void setOpenColor(const QColor& color);
    void setClosedColor(const QColor& color);

    QRectF boundingRect() const override;
    void paint(QPainter* painter, const QStyleOptionGraphicsItem* option,
        QWidget* widget) override;

protected:
    void updateAppearance() override;

private:
    qreal m_size = 30.0;           // 阀门大小
    QColor m_openColor;            // 开状态颜色
    QColor m_closedColor;          // 关状态颜色
    QColor m_currentColor;         // 当前颜色
    float m_openPercent = 100.0f;  // 开度百分比（0~100）
    QString m_bindProperty;        // 绑定的属性名（outputValue/currentValue）
};
