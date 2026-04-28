#pragma once
#include <qobject.h>
#include <qtimer.h>
#include "domain/auth/User.h"

class SessionManager : public QObject {
    Q_OBJECT
public:
    explicit SessionManager(QObject* parent = nullptr) : QObject(parent) {
        m_autoLogoutTimer = new QTimer(this);
        m_autoLogoutTimer->setSingleShot(true);
        connect(m_autoLogoutTimer, &QTimer::timeout, this, &SessionManager::onTimeout);
    }

    void setAutoLogoutTimeout(int timeoutMs) {
        m_timeoutMs = timeoutMs;
        if (timeoutMs > 0) m_autoLogoutTimer->setInterval(timeoutMs);
        else m_autoLogoutTimer->stop();
    }

    void startSession(const QString& username, int level) {
        m_currentUser = username;
        m_currentLevel = level;
        m_active = true;
        if (m_timeoutMs > 0) m_autoLogoutTimer->start();
        emit sessionStarted(username, level);
    }

    void endSession() {
        QString user = m_currentUser;
        m_currentUser.clear();
        m_currentLevel = 0;
        m_active = false;
        m_autoLogoutTimer->stop();
        emit sessionEnded(user);
    }

    void resetTimer() {
        if (m_active && m_timeoutMs > 0) m_autoLogoutTimer->start();
    }

    bool isActive() const { return m_active; }
    QString currentUser() const { return m_currentUser; }
    int currentLevel() const { return m_currentLevel; }

signals:
    void sessionStarted(const QString& username, int level);
    void sessionEnded(const QString& username);
    void sessionTimeout();

private slots:
    void onTimeout() { emit sessionTimeout(); endSession(); }

private:
    QTimer* m_autoLogoutTimer;
    int m_timeoutMs = 15 * 60 * 1000; // 15 min default
    QString m_currentUser;
    int m_currentLevel = 0;
    bool m_active = false;
};
