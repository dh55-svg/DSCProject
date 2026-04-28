#pragma once
#include <QGraphicsScene>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonDocument>
#include <QFile>
#include <QMap>
#include "presentation/widgets/BaseGraphicsItem.h"
#include "presentation/widgets/PipeItem.h"
#include "presentation/widgets/ValveItem.h"
#include "presentation/widgets/TankItem.h"
#include "presentation/widgets/PumpItem.h"
#include "presentation/widgets/DataLabelItem.h"

class PidScene : public QGraphicsScene {
    Q_OBJECT
public:
    explicit PidScene(QObject* parent = nullptr);
    ~PidScene() override;

    bool loadFromJson(const QString& jsonPath);
    void clearScene();
    QList<BaseGraphicsItem*> allGraphicItems() const;

signals:
    void graphicItemClicked(BaseGraphicsItem* item);

private:
    BaseGraphicsItem* createItem(const QString& type);
    QMap<QString, BaseGraphicsItem*> m_items;
};
