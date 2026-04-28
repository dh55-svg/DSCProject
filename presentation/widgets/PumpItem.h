#pragma once
#include "BaseGraphicsItem.h"
#include <QPainter>

class PumpItem : public BaseGraphicsItem {
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
    qreal m_size = 30.0;
    bool m_running = false;
    QColor m_bodyColor = QColor(60, 180, 60);
    qreal m_rotationAngle = 0.0;
};
