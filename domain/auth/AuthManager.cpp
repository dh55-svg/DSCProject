#include "AuthManager.h"
#include <QCoreApplication>
#include <QDateTime>

AuthManager::AuthManager(IUserRepo& userRepo, IOperationRepo& opRepo, ILogger* logger)
    : m_userRepo(userRepo), m_opRepo(opRepo), m_logger(logger) {}

void AuthManager::initialize() {
    // Default users (in production, load from DB via IUserRepo)
    User admin{"admin", PasswordHasher::hash("admin123"), "", 3};
    User engineer{"engineer", PasswordHasher::hash("eng123"), "", 2};
    User oper{"operator", PasswordHasher::hash("op123"), "", 1};
    User observer{"observer", PasswordHasher::hash("obs123"), "", 0};

    m_users["admin"] = admin;
    m_users["engineer"] = engineer;
    m_users["operator"] = oper;
    m_users["observer"] = observer;

    m_session.setAutoLogoutTimeout(15 * 60 * 1000);

    connect(&m_session, &SessionManager::sessionTimeout, this, [this]() {
        if (m_logger) m_logger->info("操作超时，自动登出");
        emit userLoggedOut();
    });

    connect(qApp, &QCoreApplication::aboutToQuit, this, &AuthManager::shutdown);

    if (m_logger) m_logger->info("权限管理器初始化完成");
}

void AuthManager::shutdown() {
    m_session.endSession();
}

bool AuthManager::login(const QString& username, const QString& password) {
    QMutexLocker lock(&m_mutex);
    auto it = m_users.find(username);
    if (it == m_users.end()) {
        if (m_logger) m_logger->warn(QString("登录失败：用户不存在 - %1").arg(username));
        return false;
    }

    if (!PasswordHasher::verify(password, it->passwordHash)) {
        if (m_logger) m_logger->warn(QString("登录失败：密码错误 - %1").arg(username));
        return false;
    }

    m_session.startSession(username, it->userLevel);

    lock.unlock();
    emit userLoggedIn(username, it->userLevel);
    logAction("用户登录", username);
    return true;
}

void AuthManager::logout() {
    QMutexLocker lock(&m_mutex);
    QString user = m_session.currentUser();
    m_session.endSession();
    lock.unlock();

    if (m_logger) m_logger->info(QString("用户登出: %1").arg(user));
    logAction("用户登出", user);
    emit userLoggedOut();
}

bool AuthManager::isLoggedIn() const { return m_session.isActive(); }
QString AuthManager::currentUsername() const { return m_session.currentUser(); }
int AuthManager::currentUserLevel() const { return m_session.currentLevel(); }

bool AuthManager::hasPermission(int requiredLevel) const {
    return m_session.currentLevel() >= requiredLevel;
}

bool AuthManager::canOperate() const { return hasPermission(1); }
bool AuthManager::canConfigure() const { return hasPermission(2); }

bool AuthManager::confirmCriticalAction(const QString& action, const QString& detail) {
    if (!canOperate()) {
        emit permissionDenied(action);
        return false;
    }
    logAction(action, detail);
    return true;
}

void AuthManager::logAction(const QString& action, const QString& detail) {
    qint64 now = QDateTime::currentMSecsSinceEpoch();
    m_opRepo.insertLog(m_session.currentUser().isEmpty() ? "system" : m_session.currentUser(),
                       action, detail, now);
}

void AuthManager::setAutoLogoutTimeout(int timeoutMs) {
    m_session.setAutoLogoutTimeout(timeoutMs);
}

void AuthManager::resetAutoLogoutTimer() {
    m_session.resetTimer();
}
