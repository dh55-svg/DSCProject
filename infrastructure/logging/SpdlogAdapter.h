#pragma once
#include "infrastructure/logging/ILogger.h"
#include <qstring.h>
#include <qfile.h>
#include <qtextstream.h>
#include <qmutex.h>
#include <qdir.h>
#include <qdatetime.h>
#include <functional>

class SpdlogAdapter : public ILogger {
public:
    SpdlogAdapter();
    ~SpdlogAdapter();

    void log(LogLevel level, const QString& msg) override;
    void debug(const QString& msg) override { log(LogLevel::Debug, msg); }
    void info(const QString& msg) override { log(LogLevel::Info, msg); }
    void warn(const QString& msg) override { log(LogLevel::Warn, msg); }
    void error(const QString& msg) override { log(LogLevel::Error, msg); }
    void setLevel(LogLevel level) override { m_level = level; }
    void setLogCallback(std::function<void(const QString&)> callback) override { m_callback = std::move(callback); }
    void setLogDir(const QString& dir);

private:
    void checkRotation();
    bool openLogFile();
    static QString levelToString(LogLevel level);

    QString m_logDir = "./logs";
    LogLevel m_level = LogLevel::Info;
    int m_maxFileSize = 50;
    int m_maxRotationCount = 30;
    QFile m_currentFile;
    QTextStream m_stream;
    QMutex m_mutex;
    qint64 m_currentSize = 0;
    std::function<void(const QString&)> m_callback;
};
