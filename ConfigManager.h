#pragma once
#include "core_global.h"
#include "DataEngine.h"
#include "PidScene.h"
#include <QObject>
#include <QString>
#include <QMutex>

/**
 * @brief 统一JSON配置管理器
 *
 * 职责：
 * - 加载 tags.json → 喂给 DataEngine / TagConfigMgr
 * - 加载 scene.json → 喂给 PidScene
 * - 管理配置文件路径
 * - 提供项目打开/保存功能
 */
class CORE_EXPORT ConfigManager : public QObject {
    Q_OBJECT
public:
    static ConfigManager& instance();

    // 初始化：加载所有默认配置
    bool initialize(DataEngine* engine, PidScene* scene);

    // 加载/保存 位号配置
    bool loadTags(const QString& jsonPath, DataEngine* engine);
    bool saveTags(const QString& jsonPath) const;

    // 加载/保存 场景配置
    bool loadScene(const QString& jsonPath, PidScene* scene);
    bool saveScene(const QString& jsonPath, PidScene* scene) const;

    // 默认路径访问
    QString defaultTagsPath() const { return m_basePath + "/tags.json"; }
    QString defaultScenePath() const { return m_basePath + "/scene.json"; }
    void setBasePath(const QString& path);

signals:
    void configLoaded(const QString& type);  // "tags" / "scene"
    void configSaved(const QString& type);
    void loadError(const QString& type, const QString& error);

private:
    ConfigManager();
    ~ConfigManager() override = default;
    ConfigManager(const ConfigManager&) = delete;
    ConfigManager& operator=(const ConfigManager&) = delete;

    QString m_basePath = "./config";
    mutable QMutex m_mutex;
};
