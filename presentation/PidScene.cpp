#include "PidScene.h"
#include <QGraphicsSceneMouseEvent>

PidScene::PidScene(QObject* parent)
    : QGraphicsScene(parent)
{
    setSceneRect(-500, -500, 2000, 1500);
    setBackgroundBrush(QColor(26, 26, 46));
}

PidScene::~PidScene() {}

BaseGraphicsItem* PidScene::createItem(const QString& type) {
    if (type == "Pipe")       return new PipeItem();
    if (type == "Valve")      return new ValveItem();
    if (type == "Tank")       return new TankItem();
    if (type == "Pump")       return new PumpItem();
    if (type == "Label")      return new DataLabelItem();
    return nullptr;
}

bool PidScene::loadFromJson(const QString& jsonPath) {
    QFile file(jsonPath);
    if (!file.open(QIODevice::ReadOnly)) return false;

    QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    file.close();

    if (!doc.isObject()) return false;

    QJsonObject root = doc.object();
    QJsonArray items = root.value("items").toArray();

    clearScene();

    for (const QJsonValue& val : items) {
        QJsonObject obj = val.toObject();
        QString type = obj.value("type").toString();
        BaseGraphicsItem* item = createItem(type);
        if (!item) continue;

        item->loadFromJson(obj);
        item->setPos(obj.value("x").toDouble(0), obj.value("y").toDouble(0));

        if (obj.contains("rotation"))
            item->setRotation(obj.value("rotation").toDouble(0));

        QString tagname = obj.value("tagName").toString();
        if (tagname.isEmpty())
            tagname = obj.value("binding_tag").toString();
        if (!tagname.isEmpty()) {
            item->bindTag(tagname);
            m_items[tagname] = item;
        }

        connect(item, &BaseGraphicsItem::itemClicked, this, &PidScene::graphicItemClicked);
        addItem(item);
    }
    return true;
}

void PidScene::clearScene() {
    m_items.clear();
    QGraphicsScene::clear();
    setBackgroundBrush(QColor(26, 26, 46));
}

QList<BaseGraphicsItem*> PidScene::allGraphicItems() const {
    return m_items.values();
}
