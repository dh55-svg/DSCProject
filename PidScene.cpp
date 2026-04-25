#include "PidScene.h"
#include "logger.h"
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>

PidScene::PidScene(QObject* parent)
    : QGraphicsScene(parent)
{
    setBackgroundBrush(QColor(20, 25, 30));
}

PidScene::~PidScene()
{
    clearScene();
}

bool PidScene::loadFromJson(const QString& jsonPath)
{
    QFile file(jsonPath);
    if (!file.open(QIODevice::ReadOnly)) {
        LOG_ERROR("PidScene", QString("无法打开场景文件: %1").arg(jsonPath));
        return false;
    }

    QJsonParseError error;
    QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &error);
    file.close();

    if (error.error != QJsonParseError::NoError) {
        LOG_ERROR("PidScene", QString("JSON解析失败: %1").arg(error.errorString()));
        return false;
    }

    QJsonObject root = doc.object();

    // 设置场景大小
    int w = root["sceneWidth"].toInt(1200);
    int h = root["sceneHeight"].toInt(800);
    setSceneRect(0, 0, w, h);

    // 设置背景色
    if (root.contains("backgroundColor")) {
        setBackgroundBrush(QColor(root["backgroundColor"].toString()));
    }

    // 加载所有图元
    QJsonArray items = root["items"].toArray();
    int loaded = 0;
    for (const QJsonValue& val : items) {
        QJsonObject itemObj = val.toObject();
        QString type = itemObj["type"].toString();
        if (type.isEmpty()) {
            LOG_WARN("PidScene", "跳过缺少type的图元");
            continue;
        }

        BaseGraphicsItem* item = createItem(type);
        if (!item) {
            LOG_WARN("PidScene", QString("未知图元类型: %1，已跳过").arg(type));
            continue;
        }

        // 从JSON加载图元属性
        item->loadFromJson(itemObj);
        addItem(item);

        // 存储到索引（用于按名称查找）
        QString id = itemObj["id"].toString();
        if (id.isEmpty()) {
            id = QString("%1_%2").arg(type).arg(loaded);
        }
        m_items.insert(id, item);

        loaded++;
    }

    LOG_INFO("PidScene", QString("场景加载完成: %1，共%2个图元")
        .arg(root["sceneName"].toString("未命名")).arg(loaded));
    return true;
}

void PidScene::clearScene()
{
    // 移除所有图元
    for (auto* item : m_items) {
        removeItem(item);
        delete item;
    }
    m_items.clear();
    clear();
    LOG_INFO("PidScene", "场景已清空");
}

QList<BaseGraphicsItem*> PidScene::allGraphicItems() const
{
    return m_items.values();
}

BaseGraphicsItem* PidScene::createItem(const QString& type)
{
    if (type == "Tank") {
        return new TankItem();
    } else if (type == "Pipe") {
        return new PipeItem();
    } else if (type == "Valve") {
        return new ValveItem();
    } else if (type == "Pump") {
        return new PumpItem();
    } else if (type == "Label") {
        return new DataLabelItem();
    }
    return nullptr;
}
