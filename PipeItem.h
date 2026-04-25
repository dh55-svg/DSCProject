#pragma once


#include "export.h"
#include "BaseGraphicsItem.h"
#include <QPainter>
#include <QTimer>
#include <QPainterPath>

/**
 * @brief 动态管道图元
 *
 * 化工P&ID图上的管道，需要表现流体流动方向。
 *
 * 实现原理：
 * - 管道本体是一个矩形（水平或垂直）
 * - 内部有流动动画：虚线或颜色渐变沿管道方向移动
 * - 使用QTimer每100ms更新动画偏移量，让虚线"流动"起来
 * - 流动方向和速度可以配置
 *
 * 踩坑经验：
 * - 管道动画不能用QPropertyAnimation，几百根管道同时动画CPU会爆
 * - 用一个全局定时器统一驱动所有管道动画，而不是每根管道一个定时器
 * - paint()里不能创建QPainterPath对象，要预计算好
 */
class GRAPHICS_EXPORT PipeItem : public BaseGraphicsItem {
    Q_OBJECT

public:
    enum FlowDirection {
        LeftToRight = 0,
        RightToLeft = 1,
        TopToBottom = 2,
        BottomToTop = 3
    };

    explicit PipeItem(QGraphicsItem* parent = nullptr);

    QString itemTypeName() const override { return "Pipe"; }
    void loadFromJson(const QJsonObject& json) override;

    // 管道属性
    void setLength(qreal length);
    void setWidth(qreal width);
    void setFlowDirection(FlowDirection dir);
    void setFlowing(bool flowing);

    QRectF boundingRect() const override;
    void paint(QPainter* painter, const QStyleOptionGraphicsItem* option,
        QWidget* widget) override;

protected:
    void updateAppearance() override;

private:
    qreal m_length = 100.0;        // 管道长度
    qreal m_width = 8.0;           // 管道宽度
    FlowDirection m_flowDir = LeftToRight;
    bool m_flowing = true;         // 是否有流体通过
    qreal m_animOffset = 0.0;     // 动画偏移量
    QColor m_pipeColor;            // 管道颜色
    QColor m_fluidColor;           // 流体颜色

    static QTimer* s_animTimer;    // 全局动画定时器（所有管道共享）
    static qreal s_globalOffset;   // 全局动画偏移量
    static int s_pipeCount;        // 管道实例计数

    void initAnimation();
};
