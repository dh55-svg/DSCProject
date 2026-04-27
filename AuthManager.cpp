#include "AuthManager.h"
#include "logger.h"
#include "DatabaseManager.h"
#include <QCoreApplication>
#include <qcryptographichash.h>
#include <QRandomGenerator>
AuthManager& AuthManager::instance()
{
	static AuthManager instance;
	return instance;
}
void AuthManager::initialize()
{
	// 初始化默认用户（生产环境应从数据库加载）
	// 密码使用SHA-256哈希存储，不存明文

	UserInfo admin;
	admin.username = "admin";
	admin.passwordHash = hashPassword("admin123");
	admin.level = UserLevel::Admin;
	m_users["admin"] = admin;

	UserInfo engineer;
	engineer.username = "engineer";
	engineer.passwordHash = hashPassword("eng123");
	engineer.level = UserLevel::Engineer;
	m_users["engineer"] = engineer;

	UserInfo operator1;
	operator1.username = "operator";
	operator1.passwordHash = hashPassword("op123");
	operator1.level = UserLevel::Operator;
	m_users["operator"] = operator1;

	UserInfo observer;
	observer.username = "observer";
	observer.passwordHash = hashPassword("obs123");
	observer.level = UserLevel::Observer;
	m_users["observer"] = observer;


	m_autoLogoutTimer = new QTimer(this);
	m_autoLogoutTimer->setSingleShot(true);
	connect(m_autoLogoutTimer, &QTimer::timeout, this, &AuthManager::onAutoLogoutTimeout);
	setAutoLogoutTimeout(15*60*1000);

	LOG_INFO("AuthManager", "权限管理器初始化完成");

    // 程序退出前清理，避免 static 单例在 QApplication 析构后才销毁导致崩溃
    connect(qApp, &QCoreApplication::aboutToQuit, this, &AuthManager::shutdown);
}

void AuthManager::shutdown()
{
    if (m_autoLogoutTimer) {
        m_autoLogoutTimer->stop();
        delete m_autoLogoutTimer;
        m_autoLogoutTimer = nullptr;
    }
    LOG_INFO("AuthManager", "权限管理器已关闭");
}

bool AuthManager::login(const QString& username, const QString& password)
{
	QMutexLocker locker(&m_mutex);
	auto it = m_users.find(username);
	if (it == m_users.end()) {
		LOG_WARN("AuthManager", QString("登录失败：用户不存在 - %1").arg(username));
		return false;
	}

	bool passwordOk = false;
	QString storedHash = it->passwordHash;

	// 检查是否为新的 salted hash 格式（"iterations$salt$hash"）
	if (storedHash.contains('$')) {
		QStringList parts = storedHash.split('$');
		if (parts.size() == 3) {
			int iterations = parts[0].toInt();
			QString salt = parts[1];
			QString expectedHash = parts[2];

			// 使用相同 salt 和迭代次数重新计算
			QByteArray work = (salt + password).toUtf8();
			for (int i = 0; i < iterations; ++i) {
				work = QCryptographicHash::hash(work, QCryptographicHash::Sha256);
			}
			if (QString::fromLatin1(work.toHex()) == expectedHash) {
				passwordOk = true;
			}
		}
	} else {
		// 向后兼容旧格式（纯 SHA-256 无盐）
		QByteArray hash = QCryptographicHash::hash(
			password.toUtf8(), QCryptographicHash::Sha256);
		if (QString::fromLatin1(hash.toHex()) == storedHash) {
			passwordOk = true;
		}
	}

	if (!passwordOk) {
		LOG_WARN("AuthManager", QString("登录失败：密码错误 - %1").arg(username));
		return false;
	}
	m_currentUser = username;
	m_currentLevel = it->level;

	// 启动自动登出计时
	m_autoLogoutTimer->start();

	locker.unlock();
	emit userLoggedIn(username, it->level);

	LOG_INFO("AuthManager", QString("用户登录: %1, 权限等级=%2")
		.arg(username).arg(static_cast<int>(it->level)));

	// 记录操作日志
	logAction("用户登录", username);
	return true;


}

void AuthManager::logout()
{
	QMutexLocker locker(&m_mutex);

	QString user = m_currentUser;
	m_currentUser.clear();
	m_currentLevel = UserLevel::Observer;
	m_autoLogoutTimer->stop();
	locker.unlock();
	emit userLoggedOut();

	LOG_INFO("AuthManager", QString("用户登出: %1").arg(user));
	logAction("用户登出", user);
}

bool AuthManager::isLoggedIn() const
{
	QMutexLocker locker(&m_mutex);
	return !m_currentUser.isEmpty();
}

QString AuthManager::currentUsername() const
{
	QMutexLocker locker(&m_mutex);
	return m_currentUser;
}

UserLevel AuthManager::currentUserLevel() const
{
	QMutexLocker locker(&m_mutex);
	return m_currentLevel;
}

bool AuthManager::hasPermission(UserLevel requiredLevel) const
{
	QMutexLocker locker(&m_mutex);
	return static_cast<int>(m_currentLevel) >= static_cast<int>(requiredLevel);
}

bool AuthManager::canOperate() const
{
	return hasPermission(UserLevel::Operator);
}

bool AuthManager::canConfigure() const
{
	return hasPermission(UserLevel::Engineer);
}

bool AuthManager::confirmCriticalAction(const QString& action, const QString& detail)
{
	// 关键操作二次确认：
	// 1. 检查当前用户是否有操作权限
	if (!canOperate())
	{
		emit permissionDenied(action);
		LOG_WARN("AuthManager", QString("权限不足，拒绝操作: %1").arg(action));
		return false;
	}
	// 2. 记录操作日志（无论确认与否都记录）
	logAction(action, detail);
	// 注意：实际的密码确认对话框由UI层实现
	// 这里只做权限检查，UI层调用此方法后应弹出确认框
	return true;

}

void AuthManager::logAction(const QString& action, const QString& detail)
{
	qint64 now = QDateTime::currentMSecsSinceEpoch();
	DatabaseManager::instance().insertOperationLog(
		m_currentUser.isEmpty() ? "system" : m_currentUser,
		action, detail, now);
}

void AuthManager::setAutoLogoutTimeout(int timeoutMs)
{
	if (m_autoLogoutTimer) {
		m_autoLogoutTimer->setInterval(timeoutMs > 0 ? timeoutMs : 0);
	}
}

void AuthManager::resetAutoLogoutTimer()
{
	if (m_autoLogoutTimer && m_autoLogoutTimer->interval() > 0) {
		m_autoLogoutTimer->start();
	}
}

QString AuthManager::hashPassword(const QString& password) const
{
	// 生成随机盐（16字节 = 32 hex chars）
	QByteArray salt;
	for (int i = 0; i < 16; ++i) {
		salt.append(static_cast<char>(QRandomGenerator::global()->bounded(256)));
	}
	QString saltHex = QString::fromLatin1(salt.toHex());

	// PBKDF2-SHA256: 10000 轮迭代（生产环境应使用 bcrypt/argon2id）
	const int iterations = 10000;
	QByteArray work = (saltHex + password).toUtf8();
	for (int i = 0; i < iterations; ++i) {
		work = QCryptographicHash::hash(work, QCryptographicHash::Sha256);
	}

	// 格式: iterations$salt$hash
	return QStringLiteral("%1$%2$%3")
		.arg(iterations)
		.arg(saltHex)
		.arg(QString::fromLatin1(work.toHex()));
}

void AuthManager::onAutoLogoutTimeout()
{
	LOG_INFO("AuthManager", "操作超时，自动登出");
	logout();
}
