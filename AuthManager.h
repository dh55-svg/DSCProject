#pragma once
#include "export.h"
#include <QObject>
#include <QString>
#include <QHash>
#include <QMutex>
#include <QDateTime>
#include <QTimer>
/**
 * @brief 用户权限等级
 *
 * 化工厂DCS权限设计（遵循ISA-101人机界面标准）：
 * - Observer:  只能看，不能操作（参观人员、管理层）
 * - Operator:  可以改SP、切手动/自动（当班操作员）
 * - Engineer:  可以改PID参数、报警限值、量程（仪表工程师）
 * - Admin:     可以改系统配置、用户管理（系统管理员）
 *
 * 踩坑经验：
 * - 化工厂误关一个阀门可能引发爆炸，权限控制不是可选项
 * - 关键设备操作必须二次确认+密码验证
 * - 所有操作必须记录操作日志（谁、什么时间、做了什么）
 * - 我之前见过一个系统没有权限控制，实习操作员误关了进料阀
 *   导致反应釜压力飙升，差点出事故
 */
enum class UserLevel {
    Observer = 0,
    Operator = 1,
    Engineer = 2,
    Admin = 3
};
/**
 * @brief 权限管理器
 *
 * 负责用户认证、权限校验、操作审计。
 *
 * 设计要点：
 * 1. 登录验证：用户名+密码，密码不能明文存储
 * 2. 操作权限：每次关键操作前检查当前用户权限
 * 3. 二次确认：关键设备操作弹出确认框+密码输入
 * 4. 操作日志：所有操作写入数据库，不可篡改
 * 5. 自动登出：操作员离开超过指定时间自动登出
 */
class BUSINESS_EXPORT AuthManager :public QObject {
    Q_OBJECT
public:
    static AuthManager& instance();

    void initialize();
    void shutdown();

    // 用户登录/登出
    bool login(const QString& username, const QString& password);
    void logout();
    bool isLoggedIn() const;

    // 当前用户信息
    QString currentUsername() const;
    UserLevel currentUserLevel() const;

    // 权限检查
    bool hasPermission(UserLevel requiredLevel) const;
    bool canOperate() const;       // 操作员及以上
    bool canConfigure() const;     // 工程师及以上

    // 二次确认（关键操作前调用，返回true表示确认通过）
    bool confirmCriticalAction(const QString& action, const QString& detail);

    // 记录操作日志
    void logAction(const QString& action, const QString& detail = QString());

    // 自动登出设置
    void setAutoLogoutTimeout(int timeoutMs); // 0=禁用
    void resetAutoLogoutTimer();               // 有操作时重置计时器

signals:
    void userLoggedIn(const QString& username, UserLevel level);
    void userLoggedOut();
    void permissionDenied(const QString& action);

private slots:
    void onAutoLogoutTimeout();
private:
    AuthManager() = default;
    ~AuthManager() override = default;
    AuthManager(const AuthManager&) = delete;
    AuthManager& operator=(const AuthManager&) = delete;
    // 简单密码哈希（生产环境应使用bcrypt/argon2）
    QString hashPassword(const QString& password) const;

    struct UserInfo {
        QString username;
        QString passwordHash;
        UserLevel level;
    };

    QHash<QString, UserInfo> m_users;        // 用户表
    QString m_currentUser;                    // 当前登录用户
    UserLevel m_currentLevel = UserLevel::Observer;
    QTimer* m_autoLogoutTimer = nullptr;      // 自动登出定时器
    mutable QMutex m_mutex;                           // 互斥锁
};



