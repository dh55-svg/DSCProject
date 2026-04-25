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

        // Use 50ms on-delay so alarm triggers within one timer tick
        alarm.triggerAlarm(1, AlarmLimit::High, 85.0f, 80.0f,
                           AlarmPriority::Major, AlarmClassification::Process, 50);
        pumpEvents(600);  // Wait for on-delay + timer tick
        QCOMPARE(alarm.activeAlarmCount(), 1);
    }

    void testSeverityLevels()
    {
        auto& alarm = AlarmEngine::instance();
        alarm.triggerAlarm(10, AlarmLimit::HighHigh, 95.0f, 90.0f,
                           AlarmPriority::Critical, AlarmClassification::Process, 50);
        alarm.triggerAlarm(11, AlarmLimit::High, 85.0f, 80.0f,
                           AlarmPriority::Major, AlarmClassification::Process, 50);
        alarm.triggerAlarm(12, AlarmLimit::Low, 15.0f, 20.0f,
                           AlarmPriority::Major, AlarmClassification::Process, 50);
        alarm.triggerAlarm(13, AlarmLimit::LowLow, 3.0f, 5.0f,
                           AlarmPriority::Critical, AlarmClassification::Process, 50);
        pumpEvents(600);

        QVERIFY(alarm.activeAlarmCount() >= 4);
        QCOMPARE(alarm.activeAlarmCount(AlarmLimit::HighHigh), 1);
        QCOMPARE(alarm.activeAlarmCount(AlarmLimit::High), 1);
        QCOMPARE(alarm.activeAlarmCount(AlarmLimit::Low), 1);
        QCOMPARE(alarm.activeAlarmCount(AlarmLimit::LowLow), 1);
    }

    void testDuplicateTriggerIgnored()
    {
        auto& alarm = AlarmEngine::instance();
        int before = alarm.activeAlarmCount();

        // Same tagId + same limit should be deduped
        alarm.triggerAlarm(1, AlarmLimit::High, 85.0f, 80.0f,
                           AlarmPriority::Major, AlarmClassification::Process, 50);
        pumpEvents(600);

        QCOMPARE(alarm.activeAlarmCount(), before);
    }

    void testAlarmEscalation()
    {
        auto& alarm = AlarmEngine::instance();

        // tag 20 starts with High
        alarm.triggerAlarm(20, AlarmLimit::High, 85.0f, 80.0f,
                           AlarmPriority::Major, AlarmClassification::Process, 50);
        pumpEvents(600);

        // escalate to HighHigh
        alarm.triggerAlarm(20, AlarmLimit::HighHigh, 95.0f, 90.0f,
                           AlarmPriority::Critical, AlarmClassification::Process, 50);
        pumpEvents(600);

        QCOMPARE(alarm.activeAlarmCount(AlarmLimit::High), 0);
        QCOMPARE(alarm.activeAlarmCount(AlarmLimit::HighHigh), 1);
    }

    void testAcknowledge()
    {
        auto& alarm = AlarmEngine::instance();

        alarm.triggerAlarm(30, AlarmLimit::High, 85.0f, 80.0f,
                           AlarmPriority::Major, AlarmClassification::Process, 50);
        pumpEvents(600);

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

        // After ack, alarm remains active but acknowledged
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

    void testClearAlarmAndAckRTN()
    {
        auto& alarm = AlarmEngine::instance();

        int before = alarm.activeAlarmCount();
        alarm.triggerAlarm(40, AlarmLimit::High, 85.0f, 80.0f,
                           AlarmPriority::Major, AlarmClassification::Process, 50);
        pumpEvents(600);
        QVERIFY(alarm.activeAlarmCount() > before);

        // clearAlarm transitions to RTN, does NOT remove from active list
        alarm.clearAlarm(40, 75.0f);
        pumpEvents();

        // tag 40 should now be in RTN unacknowledged state
        AlarmEvent ev = alarm.alarmByTagId(40);
        QCOMPARE(ev.tagId, 40u);
        QCOMPARE(ev.state, AlarmState::ReturnToNormalUnacknowledged);

        // Acknowledge RTN to fully remove
        alarm.acknowledgeReturnToNormalByTagId(40);
        pumpEvents();

        // Now tag 40 should be gone from active
        ev = alarm.alarmByTagId(40);
        QCOMPARE(ev.tagId, 0u);  // default-constructed AlarmEvent
    }

    void testHistorySize()
    {
        auto& alarm = AlarmEngine::instance();
        auto all = alarm.alarmHistory(10);
        QVERIFY(all.size() > 0);
        QVERIFY(all.size() <= 10);

        auto full = alarm.alarmHistory();
        QVERIFY(full.size() > 0);
    }

    void testAcknowledgeByTagId()
    {
        auto& alarm = AlarmEngine::instance();

        alarm.triggerAlarm(50, AlarmLimit::Low, 5.0f, 10.0f,
                           AlarmPriority::Major, AlarmClassification::Process, 50);
        pumpEvents(600);

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
        alarm.setSoundEnabled(false);
        alarm.setSoundEnabled(true);
    }

    void testShelveAndUnshelve()
    {
        auto& alarm = AlarmEngine::instance();

        alarm.triggerAlarm(60, AlarmLimit::High, 85.0f, 80.0f,
                           AlarmPriority::Major, AlarmClassification::Process, 50);
        pumpEvents(600);
        QVERIFY(alarm.activeAlarmCount() > 0);

        // Shelve with 1-hour duration
        alarm.shelveAlarm(60, "Known issue, awaiting parts", 3600);
        pumpEvents();

        auto shelved = alarm.shelvedAlarms();
        bool found = false;
        for (const auto& s : shelved) {
            if (s.tagId == 60) { found = true; break; }
        }
        QVERIFY(found);

        // Unshelve
        alarm.unshelveAlarm(60);
        pumpEvents();
        shelved = alarm.shelvedAlarms();
        found = false;
        for (const auto& s : shelved) {
            if (s.tagId == 60) { found = true; break; }
        }
        QVERIFY(!found);
    }

    void cleanupTestCase()
    {
        // Clean up any remaining alarms
        AlarmEngine::instance().acknowledgeAll();
        AlarmEngine::instance().acknowledgeAllReturnToNormal();
    }
};

QTEST_MAIN(TestAlarmEngine)
#include "TestAlarmEngine.moc"
