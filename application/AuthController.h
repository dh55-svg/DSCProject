#pragma once
#include <QObject>
#include "domain/auth/AuthManager.h"

class AuthController : public QObject {
    Q_OBJECT
public:
    explicit AuthController(AuthManager& auth, ILogger* logger = nullptr);

    AuthManager& auth() { return m_auth; }

    bool login(const QString& username, const QString& password);
    void logout();
    bool isLoggedIn() const;
    QString currentUser() const;
    int userLevel() const;
    bool canOperate() const;
    bool canConfigure() const;

signals:
    void userLoggedIn(const QString& username, int level);
    void userLoggedOut();
    void permissionDenied(const QString& action);

private:
    AuthManager& m_auth;
    ILogger* m_logger;
};
