#pragma once
#include "core_global.h"
#include <QString>
#include <QTextStream>
#include <QMutex>
#include <QDir>
#include <QFile>
#include <QMutexLocker>
#include <QDateTime>
#include <QStringList>
#include <functional>

enum class Log_Level {
    Debug = 0,
    Info,
    Warning,
    Error,
    Fatal
};

class CORE_EXPORT Logger {
public:
    static Logger& instance();
    using LogCallback = std::function<void(Log_Level, const QString&, const QString&)>;
    static void setLogCallback(LogCallback cb);

    void setLogDir(const QString& dir);
    void setLogLevel(Log_Level level);

    void setLogMaxFileSize(int sizeMb);
    void setLogRotationCount(int count);

    void log(Log_Level log, const QString& model, const QString& message);

private:
    Logger() = default;
    ~Logger();
    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

    void checkRotation();
    bool openLogFile();
    static QString LevelToString(Log_Level level);

    QString m_logDir;
    Log_Level m_level = Log_Level::Info;
    int m_maxFileSize = 50;
    int m_maxRotationCount = 30;

    QFile m_currentFile;
    QTextStream m_stream;
    QMutex m_mutex;
    qint64 m_currentSize = 0;
};

#define LOG_DEBUG(model, message)  Logger::instance().log(Log_Level::Debug, model, message)
#define LOG_INFO(model, message)   Logger::instance().log(Log_Level::Info, model, message)
#define LOG_WARN(model, message)   Logger::instance().log(Log_Level::Warning, model, message)
#define LOG_ERROR(model, message)  Logger::instance().log(Log_Level::Error, model, message)
#define LOG_FATAL(model, message)  Logger::instance().log(Log_Level::Fatal, model, message)
