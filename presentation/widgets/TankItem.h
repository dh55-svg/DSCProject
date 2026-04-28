#pragma once
#include "BaseGraphicsItem.h"
#include <QPainter>

class TankItem : public BaseGraphicsItem {
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
    qreal m_width = 60.0;
    qreal m_height = 100.0;
    float m_levelPercent = 50.0f;
    QColor m_liquidColor = QColor(40, 120, 220);
    QColor m_tankColor = QColor(140, 140, 160);
};
