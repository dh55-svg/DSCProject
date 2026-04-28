#pragma once
#include <qstring.h>
#include <functional>

enum class LogLevel {
    Debug = 0,
    Info  = 1,
    Warn  = 2,
    Error = 3,
    Fatal = 4
};

class ILogger {
public:
    virtual ~ILogger() = default;
    virtual void log(LogLevel level, const QString& msg) = 0;
    virtual void debug(const QString& msg) = 0;
    virtual void info(const QString& msg) = 0;
    virtual void warn(const QString& msg) = 0;
    virtual void error(const QString& msg) = 0;
    virtual void setLevel(LogLevel level) = 0;
    virtual void setLogCallback(std::function<void(const QString&)> callback) = 0;
};
