#pragma once

#include "export.h"
#include "BaseGraphicsItem.h"
#include <QPainter>

class GRAPHICS_EXPORT DataLabelItem : public BaseGraphicsItem {
    Q_OBJECT

public:
    explicit DataLabelItem(QGraphicsItem* parent = nullptr);

    QString itemTypeName() const override { return "Label"; }
    void loadFromJson(const QJsonObject& json) override;

    void setText(const QString& text);
    void setFontSize(int size);
    void setTextColor(const QColor& color);

    QRectF boundingRect() const override;
    void paint(QPainter* painter, const QStyleOptionGraphicsItem* option,
        QWidget* widget) override;

protected:
    void updateAppearance() override;

private:
    QString m_displayText;
    QString m_staticText;
    int m_fontSize = 12;
    QColor m_textColor = QColor(200, 200, 200);
    bool m_showValue = true;
    bool m_showAlarm = false;
    QString m_prefix;
    QString m_suffix;
};
