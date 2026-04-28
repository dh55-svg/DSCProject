#pragma once
#include <qstring.h>

class IOperationRepo {
public:
    virtual ~IOperationRepo() = default;
    virtual void insertLog(const QString& username, const QString& action, const QString& detail, qint64 timestamp) = 0;
};
