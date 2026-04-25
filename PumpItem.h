#pragma once
#include "export.h"
#include "BaseGraphicsItem.h"
#include <QPainter>
#include <QPainterPath>
/**
 * @brief 动态泵图元
 *
 * 化工P&ID图上的泵，需要表现运行/停止状态。
 *
 * 实现原理：
 * - 泵用圆形表示，内部有旋转叶片
 * - 运行状态：绿色，叶片旋转动画
 * - 停止状态：灰色，叶片静止
 * - 故障状态：红色闪烁
 */
class GRAPHICS_EXPORT PumpItem :public BaseGraphicsItem {
	Q_OBJECT
public:
    explicit PumpItem(QGraphicsItem* parent = nullptr);

    QString itemTypeName() const override { return "Pump"; }
    void loadFromJson(const QJsonObject& json) override;

    void setSize(qreal size);

    QRectF boundingRect() const override;
    void paint(QPainter* painter, const QStyleOptionGraphicsItem* option,
        QWidget* widget) override;

protected:
    void updateAppearance() override;
private:
    qreal m_size = 30.0;           // 泵大小
    bool m_running = false;        // 运行状态
    QColor m_bodyColor;            // 泵体颜色
    qreal m_rotationAngle = 0.0;  // 叶片旋转角度
};



