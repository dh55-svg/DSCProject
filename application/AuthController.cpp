#include "AuthController.h"

AuthController::AuthController(AuthManager& auth, ILogger* logger)
    : m_auth(auth), m_logger(logger)
{
    connect(&m_auth, &AuthManager::userLoggedIn, this, &AuthController::userLoggedIn);
    connect(&m_auth, &AuthManager::userLoggedOut, this, &AuthController::userLoggedOut);
    connect(&m_auth, &AuthManager::permissionDenied, this, &AuthController::permissionDenied);
}

bool AuthController::login(const QString& username, const QString& password) {
    return m_auth.login(username, password);
}

void AuthController::logout() { m_auth.logout(); }

bool AuthController::isLoggedIn() const { return m_auth.isLoggedIn(); }
QString AuthController::currentUser() const { return m_auth.currentUsername(); }
int AuthController::userLevel() const { return m_auth.currentUserLevel(); }
bool AuthController::canOperate() const { return m_auth.canOperate(); }
bool AuthController::canConfigure() const { return m_auth.canConfigure(); }
