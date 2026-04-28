#pragma once
#include <qobject.h>
#include <QHash>
#include <QMutex>
#include "domain/auth/User.h"
#include "domain/auth/PasswordHasher.h"
#include "domain/auth/SessionManager.h"
#include "infrastructure/persistence/IUserRepo.h"
#include "infrastructure/persistence/IOperationRepo.h"
#include "infrastructure/logging/ILogger.h"

class AuthManager : public QObject {
    Q_OBJECT
public:
    AuthManager(IUserRepo& userRepo, IOperationRepo& opRepo, ILogger* logger = nullptr);

    void initialize();
    void shutdown();

    bool login(const QString& username, const QString& password);
    void logout();
    bool isLoggedIn() const;
    QString currentUsername() const;
    int currentUserLevel() const;
    bool hasPermission(int requiredLevel) const;
    bool canOperate() const;
    bool canConfigure() const;

    bool confirmCriticalAction(const QString& action, const QString& detail);
    void logAction(const QString& action, const QString& detail = QString());

    void setAutoLogoutTimeout(int timeoutMs);
    void resetAutoLogoutTimer();

signals:
    void userLoggedIn(const QString& username, int level);
    void userLoggedOut();
    void permissionDenied(const QString& action);

private:
    IUserRepo& m_userRepo;
    IOperationRepo& m_opRepo;
    ILogger* m_logger;
    SessionManager m_session;
    QHash<QString, User> m_users;
    mutable QMutex m_mutex;
};
