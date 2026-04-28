#pragma once
#include "BaseGraphicsItem.h"
#include <QPainter>
#include <QTimer>

class PipeItem : public BaseGraphicsItem {
    Q_OBJECT
public:
    enum FlowDirection { LeftToRight = 0, RightToLeft = 1, TopToBottom = 2, BottomToTop = 3 };

    explicit PipeItem(QGraphicsItem* parent = nullptr);

    QString itemTypeName() const override { return "Pipe"; }
    void loadFromJson(const QJsonObject& json) override;

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
    qreal m_length = 100.0;
    qreal m_width = 8.0;
    FlowDirection m_flowDir = LeftToRight;
    bool m_flowing = true;
    qreal m_animOffset = 0.0;
    QColor m_pipeColor = QColor(100, 100, 130);
    QColor m_fluidColor = QColor(60, 140, 220);

    static QTimer* s_animTimer;
    static qreal s_globalOffset;
    static int s_pipeCount;
    void initAnimation();
};
