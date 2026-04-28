#pragma once
#include <QString>
#include <QVariantMap>
#include "infrastructure/config/IConfigRepo.h"

struct AppConfig {
    QString dbBackend = "mysql"; // "mysql" or "sqlite"
    struct MysqlConfig {
        QString host = "127.0.0.1";
        int port = 3306;
        QString database = "dcs";
        QString user = "root";
        QString password = "";
        int poolSize = 5;
    } mysql;

    struct SqliteConfig {
        QString path = "data/dcs.db";
    } sqlite;

    QString fieldbusType = "modbus"; // "modbus", "simulator", "opcua"
    QString configBasePath = "./config";

    static AppConfig fromJson(const QString& path, IConfigRepo& repo) {
        AppConfig cfg;
        auto map = repo.loadAppConfig(path);
        cfg.dbBackend = map.value("dbBackend", "sqlite").toString();
        QVariantMap m = map.value("mysql").toMap();
        if (!m.isEmpty()) {
            cfg.mysql.host = m.value("host", "127.0.0.1").toString();
            cfg.mysql.port = m.value("port", 3306).toInt();
            cfg.mysql.database = m.value("database", "dcs").toString();
            cfg.mysql.user = m.value("user", "root").toString();
            cfg.mysql.password = m.value("password", "").toString();
        }
        cfg.fieldbusType = map.value("fieldbus", "modbus").toString();
        cfg.configBasePath = map.value("configPath", "./config").toString();
        return cfg;
    }
};
