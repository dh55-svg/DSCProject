#include <QTest>
#include <QVector>
#include "../AuthManager.h"
#include "../DatabaseManager.h"
#include "../logger.h"

class TestAuthManager : public QObject {
    Q_OBJECT

private slots:
    void initTestCase()
    {
        Logger::instance().setLogLevel(Log_Level::Fatal);
        DatabaseManager::instance().initializeWithFallback(
            "localhost", 3306, "dcs_test", "root", "");
        AuthManager::instance().initialize();

        // start logged out
        AuthManager::instance().logout();
    }

    void testInitialState()
    {
        auto& auth = AuthManager::instance();
        QVERIFY(!auth.isLoggedIn());
        QCOMPARE(auth.currentUserLevel(), UserLevel::Observer);
        QVERIFY(auth.currentUsername().isEmpty());
    }

    void testLoginAdmin()
    {
        auto& auth = AuthManager::instance();
        bool ok = auth.login("admin", "admin123");
        QVERIFY(ok);
        QVERIFY(auth.isLoggedIn());
        QCOMPARE(auth.currentUsername(), QString("admin"));
        QCOMPARE(auth.currentUserLevel(), UserLevel::Admin);
    }

    void testHasPermission()
    {
        auto& auth = AuthManager::instance();
        QVERIFY(auth.hasPermission(UserLevel::Observer));
        QVERIFY(auth.hasPermission(UserLevel::Operator));
        QVERIFY(auth.hasPermission(UserLevel::Engineer));
        QVERIFY(auth.hasPermission(UserLevel::Admin));
        QVERIFY(auth.canOperate());
        QVERIFY(auth.canConfigure());
    }

    void testLoginOperator()
    {
        auto& auth = AuthManager::instance();
        auth.logout();
        QVERIFY(!auth.isLoggedIn());

        bool ok = auth.login("operator", "op123");
        QVERIFY(ok);
        QCOMPARE(auth.currentUserLevel(), UserLevel::Operator);
    }

    void testOperatorPermissions()
    {
        auto& auth = AuthManager::instance();
        QVERIFY(auth.hasPermission(UserLevel::Observer));
        QVERIFY(auth.hasPermission(UserLevel::Operator));
        QVERIFY(!auth.hasPermission(UserLevel::Engineer));
        QVERIFY(!auth.hasPermission(UserLevel::Admin));
        QVERIFY(auth.canOperate());
        QVERIFY(!auth.canConfigure());
    }

    void testLoginEngineer()
    {
        auto& auth = AuthManager::instance();
        auth.logout();

        bool ok = auth.login("engineer", "eng123");
        QVERIFY(ok);
        QCOMPARE(auth.currentUserLevel(), UserLevel::Engineer);
    }

    void testEngineerPermissions()
    {
        auto& auth = AuthManager::instance();
        QVERIFY(auth.hasPermission(UserLevel::Engineer));
        QVERIFY(auth.canConfigure());
        QVERIFY(!auth.hasPermission(UserLevel::Admin));
    }

    void testWrongPasswordFails()
    {
        auto& auth = AuthManager::instance();
        auth.logout();

        bool ok = auth.login("operator", "wrongpass");
        QVERIFY(!ok);
        QVERIFY(!auth.isLoggedIn());
    }

    void testNonexistentUserFails()
    {
        auto& auth = AuthManager::instance();
        bool ok = auth.login("nobody", "anything");
        QVERIFY(!ok);
        QVERIFY(!auth.isLoggedIn());
    }

    void testLoginObserver()
    {
        auto& auth = AuthManager::instance();
        auth.login("observer", "obs123");
        QCOMPARE(auth.currentUserLevel(), UserLevel::Observer);
    }

    void testObserverPermissions()
    {
        auto& auth = AuthManager::instance();
        QVERIFY(auth.hasPermission(UserLevel::Observer));
        QVERIFY(!auth.hasPermission(UserLevel::Operator));
        QVERIFY(!auth.canOperate());
        QVERIFY(!auth.canConfigure());
    }

    void testLogoutClearsState()
    {
        auto& auth = AuthManager::instance();
        auth.login("admin", "admin123");
        QVERIFY(auth.isLoggedIn());

        auth.logout();
        QVERIFY(!auth.isLoggedIn());
        QCOMPARE(auth.currentUserLevel(), UserLevel::Observer);
        QVERIFY(auth.currentUsername().isEmpty());
    }

    void testLoginCaseSensitive()
    {
        auto& auth = AuthManager::instance();
        auth.logout();

        // wrong case should fail
        bool ok = auth.login("Admin", "admin123");
        QVERIFY(!ok);
    }

    void testConfirmCriticalAction()
    {
        auto& auth = AuthManager::instance();
        auth.login("operator", "op123");

        bool confirmed = auth.confirmCriticalAction("测试操作", "测试详情");
        QVERIFY(confirmed);

        auth.logout();

        // observer should be denied
        auth.login("observer", "obs123");
        confirmed = auth.confirmCriticalAction("测试操作", "测试详情");
        QVERIFY(!confirmed);
    }
};

QTEST_MAIN(TestAuthManager)
#include "TestAuthManager.moc"
