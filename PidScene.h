#pragma once
#include "export.h"
#include <QGraphicsScene>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonDocument>
#include <QFile>
#include <QMap>
#include "BaseGraphicsItem.h"
#include "PipeItem.h"
#include "ValveItem.h"
#include "TankItem.h"
#include "PumpItem.h"
#include "DataLabelItem.h"
/**
 * @brief P&ID graphics scene
 *
 * Manages the P&ID canvas and all graphic items.
 *
 * Features:
 * 1. Load scene from JSON file (dynamic layout)
 * 2. Manage all graphic items in the scene
 * 3. Forward item click events to UI
 *
 * Dynamic concept:
 * DCS screens are not hard-coded by engineers - designed to be "dynamic".
 * Engineers use an editor to drag items, configure properties, bind tags,
 * save as JSON, and the runtime renders from the file.
 * A new unit only needs a JSON file update, no recompilation needed.
 */
class GRAPHICS_EXPORT PidScene :public QGraphicsScene {
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