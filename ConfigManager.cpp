#include "ConfigManager.h"
#include "DataEngine.h"
#include "PidScene.h"
#include "TagConfigMgr.h"
#include "logger.h"
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>

ConfigManager& ConfigManager::instance()
{
    static ConfigManager inst;
    return inst;
}

ConfigManager::ConfigManager()
{
}

void ConfigManager::setBasePath(const QString& path)
{
    QMutexLocker lock(&m_mutex);
    m_basePath = path;
}

bool ConfigManager::initialize(DataEngine* engine, PidScene* scene)
{
    LOG_INFO("ConfigManager", "========== 开始加载配置 ==========");
    LOG_INFO("ConfigManager", QString("配置目录: %1").arg(m_basePath));

    // 确保配置目录存在
    QDir dir(m_basePath);
    if (!dir.exists()) {
        LOG_WARN("ConfigManager", QString("配置目录不存在，尝试创建: %1").arg(m_basePath));
        dir.mkpath(".");
    }

    bool allOk = true;

    // 加载位号配置
    QString tagsPath = defaultTagsPath();
    if (QFile::exists(tagsPath)) {
        if (loadTags(tagsPath, engine)) {
            LOG_INFO("ConfigManager", "位号配置加载成功");
        } else {
            LOG_ERROR("ConfigManager", "位号配置加载失败");
            allOk = false;
        }
    } else {
        LOG_WARN("ConfigManager", QString("位号配置文件不存在: %1").arg(tagsPath));
    }

    // 加载场景配置
    QString scenePath = defaultScenePath();
    if (QFile::exists(scenePath)) {
        if (loadScene(scenePath, scene)) {
            LOG_INFO("ConfigManager", "场景配置加载成功");
        } else {
            LOG_ERROR("ConfigManager", "场景配置加载失败");
            allOk = false;
        }
    } else {
        LOG_WARN("ConfigManager", QString("场景配置文件不存在: %1").arg(scenePath));
    }

    LOG_INFO("ConfigManager", "========== 配置加载完成 ==========");
    return allOk;
}

bool ConfigManager::loadTags(const QString& jsonPath, DataEngine* engine)
{
    if (!engine) {
        LOG_ERROR("ConfigManager", "loadTags: DataEngine为空");
        emit loadError("tags", "DataEngine为空");
        return false;
    }

    if (!QFile::exists(jsonPath)) {
        QString msg = QString("文件不存在: %1").arg(jsonPath);
        LOG_ERROR("ConfigManager", msg);
        emit loadError("tags", msg);
        return false;
    }

    // 先解析JSON一次，再分别喂给DataEngine（运行时）和TagConfigMgr（配置查询）
    QFile file(jsonPath);
    if (!file.open(QIODevice::ReadOnly)) {
        emit loadError("tags", "无法打开文件");
        return false;
    }
    QJsonParseError error;
    QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &error);
    file.close();
    if (error.error != QJsonParseError::NoError) {
        emit loadError("tags", error.errorString());
        return false;
    }

    QVector<TagInfo> tagList;
    QJsonArray tagsArray = doc.array();
    for (const auto& val : tagsArray) {
        QJsonObject obj = val.toObject();
        TagInfo tag;
        tag.tagId = static_cast<quint32>(obj["tagId"].toInt());
        tag.tagName = obj["tagName"].toString();
        tag.description = obj["description"].toString();
        tag.unit = obj["unit"].toString();
        // tagType兼容字符串和整数两种格式
        if (obj["tagType"].isString()) {
            QString typeStr = obj["tagType"].toString();
            if (typeStr == "AI") tag.tagType = TagType::AI;
            else if (typeStr == "AO") tag.tagType = TagType::AO;
            else if (typeStr == "DI") tag.tagType = TagType::DI;
            else if (typeStr == "DO") tag.tagType = TagType::DO;
            else if (typeStr == "PID") tag.tagType = TagType::PID;
        } else {
            tag.tagType = static_cast<TagType>(obj["tagType"].toInt(0));
        }
        tag.engHigh = static_cast<float>(obj["engHigh"].toDouble(100.0));
        tag.engLow = static_cast<float>(obj["engLow"].toDouble(0.0));
        tag.highHighLimit = static_cast<float>(obj["highHighLimit"].toDouble(90.0));
        tag.highLimit = static_cast<float>(obj["highLimit"].toDouble(80.0));
        tag.lowLimit = static_cast<float>(obj["lowLimit"].toDouble(10.0));
        tag.lowLowLimit = static_cast<float>(obj["lowLowLimit"].toDouble(5.0));
        tag.deadband = static_cast<float>(obj["deadband"].toDouble(1.0));
        tag.modbusServerAddr = obj["modbusServerAddr"].toInt(1);
        tag.modbusRegAddr = obj["modbusRegAddr"].toInt(0);
        tag.modbusRegCount = obj["modbusRegCount"].toInt(1);
        tagList.append(tag);
    }

    // 喂给DataEngine的DataParseThread（运行时数据处理）
    if (engine) {
        // DataEngine::loadTagConfig也做了同样解析，这里复用tagList直接喂给解析线程
        engine->loadTagConfig(jsonPath);
    }

    // 喂给TagConfigMgr（配置查询：报警限值、量程、Modbus映射等）
    TagConfigMgr::instance().clear();
    QVector<TagInfo> tagsForMgr = tagList;
    TagConfigMgr::instance().addTags(tagsForMgr);

    LOG_INFO("ConfigManager", QString("位号配置加载完成: %1 个位号").arg(tagList.size()));
    emit configLoaded("tags");
    return true;
}

bool ConfigManager::saveTags(const QString& jsonPath) const
{
    QMutexLocker lock(&m_mutex);

    auto tags = TagConfigMgr::instance().getAllTags();
    QJsonArray arr;
    for (const auto& tag : tags) {
        QJsonObject obj;
        obj["tagId"] = static_cast<int>(tag.tagId);
        obj["tagName"] = tag.tagName;
        obj["description"] = tag.description;
        obj["unit"] = tag.unit;
        obj["tagType"] = static_cast<int>(tag.tagType);
        obj["engHigh"] = tag.engHigh;
        obj["engLow"] = tag.engLow;
        obj["highHighLimit"] = tag.highHighLimit;
        obj["highLimit"] = tag.highLimit;
        obj["lowLimit"] = tag.lowLimit;
        obj["lowLowLimit"] = tag.lowLowLimit;
        obj["deadband"] = tag.deadband;
        obj["modbusServerAddr"] = tag.modbusServerAddr;
        obj["modbusRegAddr"] = tag.modbusRegAddr;
        obj["modbusRegCount"] = tag.modbusRegCount;
        arr.append(obj);
    }

    QFile file(jsonPath);
    if (!file.open(QIODevice::WriteOnly)) {
        QString msg = QString("无法写入: %1").arg(jsonPath);
        LOG_ERROR("ConfigManager", msg);
        return false;
    }
    file.write(QJsonDocument(arr).toJson(QJsonDocument::Indented));
    file.close();

    LOG_INFO("ConfigManager", QString("位号配置已保存: %1 个位号 -> %2")
        .arg(arr.size()).arg(jsonPath));
    return true;
}

bool ConfigManager::loadScene(const QString& jsonPath, PidScene* scene)
{
    if (!scene) {
        LOG_ERROR("ConfigManager", "loadScene: PidScene为空");
        emit loadError("scene", "PidScene为空");
        return false;
    }

    if (!QFile::exists(jsonPath)) {
        QString msg = QString("文件不存在: %1").arg(jsonPath);
        LOG_ERROR("ConfigManager", msg);
        emit loadError("scene", msg);
        return false;
    }

    // 清空旧场景
    scene->clearScene();

    // 加载新场景
    bool ok = scene->loadFromJson(jsonPath);
    if (ok) {
        LOG_INFO("ConfigManager", "场景配置加载完成");
        emit configLoaded("scene");
    } else {
        emit loadError("scene", "场景解析失败");
    }
    return ok;
}

bool ConfigManager::saveScene(const QString& jsonPath, PidScene* scene) const
{
    if (!scene) return false;

    QJsonObject root;
    root["sceneName"] = "DCS Project";
    root["sceneWidth"] = static_cast<int>(scene->sceneRect().width());
    root["sceneHeight"] = static_cast<int>(scene->sceneRect().height());

    QJsonArray items;
    for (auto* item : scene->allGraphicItems()) {
        QJsonObject obj;
        obj["type"] = item->itemTypeName();
        obj["x"] = item->x();
        obj["y"] = item->y();
        if (!item->boundTagName().isEmpty()) {
            obj["binding_tag"] = item->boundTagName();
        }
        items.append(obj);
    }
    root["items"] = items;

    QFile file(jsonPath);
    if (!file.open(QIODevice::WriteOnly)) {
        LOG_ERROR("ConfigManager", QString("无法写入场景文件: %1").arg(jsonPath));
        return false;
    }
    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    file.close();

    LOG_INFO("ConfigManager", QString("场景已保存: %1 个图元 -> %2")
        .arg(items.size()).arg(jsonPath));
    return true;
}
