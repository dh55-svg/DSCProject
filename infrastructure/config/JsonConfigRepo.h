#pragma once
#include "infrastructure/config/IConfigRepo.h"
#include <qstring.h>

class JsonConfigRepo : public IConfigRepo {
public:
    QVariantMap loadAppConfig(const QString& path) override;
    QVariantMap loadTagsJson(const QString& path) override;
    QVariantMap loadSceneJson(const QString& path) override;
    void saveTagsJson(const QString& path, const QVariantMap& data) override;
    void saveSceneJson(const QString& path, const QVariantMap& data) override;
};
