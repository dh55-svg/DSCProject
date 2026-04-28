#pragma once
#include "BaseGraphicsItem.h"
#include <QPainter>

class ValveItem : public BaseGraphicsItem {
    Q_OBJECT
public:
    explicit ValveItem(QGraphicsItem* parent = nullptr);

    QString itemTypeName() const override { return "Valve"; }
    void loadFromJson(const QJsonObject& json) override;

    void setSize(qreal size);

    QRectF boundingRect() const override;
    void paint(QPainter* painter, const QStyleOptionGraphicsItem* option,
               QWidget* widget) override;

protected:
    void updateAppearance() override;

private:
    qreal m_size = 30.0;
    QColor m_openColor = QColor(60, 200, 60);
    QColor m_closedColor = QColor(200, 60, 60);
    QColor m_currentColor = m_openColor;
    float m_openPercent = 100.0f;
};
