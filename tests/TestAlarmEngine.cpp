#include <QTest>
#include <QVector>
#include "../AlarmEngine.h"
#include "../AuthManager.h"
#include "../DatabaseManager.h"
#include "../logger.h"

// Helper to wait for signals in a spinning event loop
static void pumpEvents(int ms = 100) {
    QElapsedTimer t;
    t.start();
    while (t.elapsed() < ms) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    }
}

class TestAlarmEngine : public QObject {
    Q_OBJECT

private slots:
    void initTestCase()
    {
        // Initialize DB with SQLite for alarm persistence
        Logger::instance().setLogLevel(Log_Level::Fatal);
        DatabaseManager::instance().initializeWithFallback(
            "localhost", 3306, "dcs_test", "root", "");
        AuthManager::instance().initialize();
        AlarmEngine::instance().initialize();
        pumpEvents();
    }

    void testTriggerAndCount()
    {
        auto& alarm = AlarmEngine::instance();

        QCOMPARE(alarm.activeAlarmCount(), 0);

        alarm.triggerAlarm(1, AlarmState::High, 85.0f, 80.0f);
        pumpEvents();
        QCOMPARE(alarm.activeAlarmCount(), 1);
    }

    void testSeverityLevels()
    {
        auto& alarm = AlarmEngine::instance();
        // trigger different severities
        alarm.triggerAlarm(10, AlarmState::HighHigh, 95.0f, 90.0f);
        alarm.triggerAlarm(11, AlarmState::High, 85.0f, 80.0f);
        alarm.triggerAlarm(12, AlarmState::Low, 15.0f, 20.0f);
        alarm.triggerAlarm(13, AlarmState::LowLow, 3.0f, 5.0f);
        pumpEvents();

        QVERIFY(alarm.activeAlarmCount() >= 4);
        QCOMPARE(alarm.activeAlarmCount(AlarmState::HighHigh), 1);
        QCOMPARE(alarm.activeAlarmCount(AlarmState::High), 1);
        QCOMPARE(alarm.activeAlarmCount(AlarmState::Low), 1);
        QCOMPARE(alarm.activeAlarmCount(AlarmState::LowLow), 1);
    }

    void testDuplicateTriggerIgnored()
    {
        auto& alarm = AlarmEngine::instance();
        int before = alarm.activeAlarmCount();

        alarm.triggerAlarm(1, AlarmState::High, 85.0f, 80.0f);
        pumpEvents();

        // alarm 1 already active, count should not increase
        QCOMPARE(alarm.activeAlarmCount(), before);
    }

    void testAlarmEscalation()
    {
        auto& alarm = AlarmEngine::instance();

        // tag 20 starts with High
        alarm.triggerAlarm(20, AlarmState::High, 85.0f, 80.0f);
        pumpEvents();

        // escalate to HighHigh
        alarm.triggerAlarm(20, AlarmState::HighHigh, 95.0f, 90.0f);
        pumpEvents();

        QCOMPARE(alarm.activeAlarmCount(AlarmState::High), 0);
        QCOMPARE(alarm.activeAlarmCount(AlarmState::HighHigh), 1);
    }

    void testAcknowledge()
    {
        auto& alarm = AlarmEngine::instance();

        alarm.triggerAlarm(30, AlarmState::High, 85.0f, 80.0f);
        pumpEvents();

        // find the alarm ID for tag 30
        QList<AlarmEvent> active = alarm.activeAlarms();
        QString alarmId;
        for (const auto& a : active) {
            if (a.tagId == 30) {
                alarmId = a.alarmId;
                break;
            }
        }
        QVERIFY(!alarmId.isEmpty());

        alarm.acknowledgeAlarm(alarmId);
        pumpEvents();

        // after acknowledge, alarm should still be active but acknowledged
        bool found = false;
        active = alarm.activeAlarms();
        for (const auto& a : active) {
            if (a.tagId == 30) {
                found = true;
                QVERIFY(a.acknowledged);
                break;
            }
        }
        QVERIFY(found);
    }

    void testAcknowledgeAll()
    {
        auto& alarm = AlarmEngine::instance();
        alarm.acknowledgeAll();
        pumpEvents();

        auto active = alarm.activeAlarms();
        for (const auto& a : active) {
            QVERIFY(a.acknowledged);
        }
    }

    void testClearAlarm()
    {
        auto& alarm = AlarmEngine::instance();

        int before = alarm.activeAlarmCount();
        alarm.triggerAlarm(40, AlarmState::High, 85.0f, 80.0f);
        pumpEvents();
        QVERIFY(alarm.activeAlarmCount() > before);

        alarm.clearAlarm(40);
        pumpEvents();

        // verify tag 40 is no longer active
        auto active = alarm.activeAlarms();
        for (const auto& a : active) {
            QVERIFY(a.tagId != 40);
        }
    }

    void testHistorySize()
    {
        auto& alarm = AlarmEngine::instance();
        auto all = alarm.allAlarms(10);
        QVERIFY(all.size() > 0);
        QVERIFY(all.size() <= 10);

        auto full = alarm.allAlarms();
        QVERIFY(full.size() > 0);
    }

    void testAcknowledgeByTagId()
    {
        auto& alarm = AlarmEngine::instance();

        alarm.triggerAlarm(50, AlarmState::Low, 5.0f, 10.0f);
        pumpEvents();

        // acknowledge via tag ID
        alarm.acknowledgeAlarmByTagId(50);
        pumpEvents();

        auto active = alarm.activeAlarms();
        for (const auto& a : active) {
            if (a.tagId == 50) {
                QVERIFY(a.acknowledged);
                return;
            }
        }
        QFAIL("tag 50 not found in active alarms after trigger");
    }

    void testSoundToggle()
    {
        auto& alarm = AlarmEngine::instance();

        // should not crash
        alarm.setSoundEnabled(false);
        alarm.setSoundEnabled(true);
    }
};

QTEST_MAIN(TestAlarmEngine)
#include "TestAlarmEngine.moc"
