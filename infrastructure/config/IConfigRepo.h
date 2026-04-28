#pragma once
#include <qstring.h>
#include <qvariant.h>

class IConfigRepo {
public:
    virtual ~IConfigRepo() = default;
    virtual QVariantMap loadAppConfig(const QString& path) = 0;
    virtual QVariantMap loadTagsJson(const QString& path) = 0;
    virtual QVariantMap loadSceneJson(const QString& path) = 0;
    virtual void saveTagsJson(const QString& path, const QVariantMap& data) = 0;
    virtual void saveSceneJson(const QString& path, const QVariantMap& data) = 0;
};
