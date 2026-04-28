#pragma once
#include <qstring.h>
#include "domain/common/AlarmState.h"

struct User {
    QString username;
    QString passwordHash;
    QString salt;
    int     userLevel = 0;       // 0=Observer, 1=Operator, 2=Engineer, 3=Admin
    QString fullName;
    bool    active = true;
    qint64  lastLogin = 0;
    qint64  createdTime = 0;
};
