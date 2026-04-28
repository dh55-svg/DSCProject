#pragma once
#include <QGraphicsObject>
#include <QJsonObject>
#include <QString>
#include <QColor>
#include "domain/common/AlarmLimit.h"
#include "domain/common/DataQuality.h"

class BaseGraphicsItem : public QGraphicsObject {
    Q_OBJECT
public:
    explicit BaseGraphicsItem(QGraphicsItem* parent = nullptr);
    ~BaseGraphicsItem() override = default;

    void bindTag(const QString& tagName);
    QString boundTagName() const { return m_tagName; }
    quint32 boundTagId() const { return m_tagId; }

    virtual void loadFromJson(const QJsonObject& json);
    virtual QString itemTypeName() const = 0;

    void onTagValueChanged(float newValue, AlarmLimit alarmState, DataQuality quality);

signals:
    void itemClicked(BaseGraphicsItem* item);
    void stateChanged();

protected:
    virtual void updateAppearance() = 0;
    float tagValue() const { return m_tagValue; }
    AlarmLimit tagAlarmState() const { return m_alarmState; }
    DataQuality tagQuality() const { return m_quality; }

    QColor alarmColor() const;
    QString qualityText() const;
    void mousePressEvent(QGraphicsSceneMouseEvent* event) override;

    QString m_tagName;
    quint32 m_tagId = 0;
    float m_tagValue = 0.0f;
    AlarmLimit m_alarmState = AlarmLimit::Normal;
    DataQuality m_quality = DataQuality::Good;
};
