#include "JsonConfigRepo.h"
#include <qfile.h>
#include <qjsondocument.h>
#include <qjsonobject.h>
#include <qdebug.h>

static QVariantMap readJsonFile(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        qWarning() << "Cannot open config file:" << path;
        return {};
    }
    QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    file.close();
    return doc.object().toVariantMap();
}

static void writeJsonFile(const QString& path, const QVariantMap& data) {
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        qWarning() << "Cannot write config file:" << path;
        return;
    }
    QJsonDocument doc(QJsonObject::fromVariantMap(data));
    file.write(doc.toJson(QJsonDocument::Indented));
    file.close();
}

QVariantMap JsonConfigRepo::loadAppConfig(const QString& path) {
    return readJsonFile(path);
}

QVariantMap JsonConfigRepo::loadTagsJson(const QString& path) {
    return readJsonFile(path);
}

QVariantMap JsonConfigRepo::loadSceneJson(const QString& path) {
    return readJsonFile(path);
}

void JsonConfigRepo::saveTagsJson(const QString& path, const QVariantMap& data) {
    writeJsonFile(path, data);
}

void JsonConfigRepo::saveSceneJson(const QString& path, const QVariantMap& data) {
    writeJsonFile(path, data);
}
