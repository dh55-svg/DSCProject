#pragma once
#include <qvector.h>
#include <qstring.h>
#include "domain/auth/User.h"

class IUserRepo {
public:
    virtual ~IUserRepo() = default;
    virtual User loadUser(const QString& username) = 0;
    virtual QVector<User> loadAll() = 0;
    virtual void updateSession(const QString& username, qint64 lastLogin) = 0;
};
