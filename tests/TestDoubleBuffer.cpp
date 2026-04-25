#include <QTest>
#include <QVector>
#include "../DoubleBuffer.h"

class TestDoubleBuffer : public QObject {
    Q_OBJECT

private slots:
    void testEmptyBuffer()
    {
        DoubleBuffer buf;
        auto all = buf.readAll();
        QVERIFY(all.isEmpty());
    }

    void testWriteAndCommitSingle()
    {
        DoubleBuffer buf;
        DoubleBuffer::TagSnapshot snap;
        snap.tagId = 1;
        snap.currentValue = 42.5f;
        snap.setPoint = 40.0f;
        snap.outputValue = 50.0f;
        snap.quality = DataQuality::Good;
        snap.alarmstate = AlarmState::Normal;
        snap.timestamp = 1000;

        buf.write(1, snap);
        QVERIFY(buf.readAll().isEmpty()); // not yet committed

        buf.commit();
        auto result = buf.readAll();
        QCOMPARE(result.size(), 1);
        QCOMPARE(result[0].tagId, 1u);
        QCOMPARE(result[0].currentValue, 42.5f);
        QCOMPARE(result[0].quality, DataQuality::Good);
    }

    void testWriteMultiple()
    {
        DoubleBuffer buf;
        DoubleBuffer::TagSnapshot s1, s2, s3;
        s1.tagId = 1; s1.currentValue = 10.0f;
        s2.tagId = 2; s2.currentValue = 20.0f;
        s3.tagId = 3; s3.currentValue = 30.0f;

        buf.write(1, s1);
        buf.write(2, s2);
        buf.write(3, s3);
        buf.commit();

        QCOMPARE(buf.readAll().size(), 3);
        QCOMPARE(buf.readTag(2).currentValue, 20.0f);
        QCOMPARE(buf.readTag(99).tagId, 0u); // non-existent
    }

    void testOverwriteCommit()
    {
        DoubleBuffer buf;
        DoubleBuffer::TagSnapshot snap;
        snap.tagId = 1; snap.currentValue = 10.0f;
        buf.write(1, snap);
        buf.commit();

        snap.currentValue = 99.0f;
        buf.write(1, snap);
        buf.commit();

        QCOMPARE(buf.readTag(1).currentValue, 99.0f);
    }

    void testUpdateWithoutCommit()
    {
        DoubleBuffer buf;
        DoubleBuffer::TagSnapshot snap;
        snap.tagId = 1; snap.currentValue = 10.0f;
        buf.write(1, snap);
        buf.commit();

        // write but don't commit -- read side unchanged
        snap.currentValue = 50.0f;
        buf.write(1, snap);

        QCOMPARE(buf.readTag(1).currentValue, 10.0f);
    }

    void testMultipleCommits()
    {
        DoubleBuffer buf;
        DoubleBuffer::TagSnapshot snap;
        snap.tagId = 1; snap.currentValue = 0.0f;

        for (int i = 1; i <= 5; ++i) {
            snap.currentValue = static_cast<float>(i * 10);
            buf.write(1, snap);
            buf.commit();
            QCOMPARE(buf.readTag(1).currentValue, static_cast<float>(i * 10));
        }
    }

    void testReadTagFields()
    {
        DoubleBuffer buf;
        DoubleBuffer::TagSnapshot snap;
        snap.tagId = 42;
        snap.currentValue = 3.14f;
        snap.setPoint = 5.0f;
        snap.outputValue = 80.0f;
        snap.quality = DataQuality::Bad;
        snap.alarmstate = AlarmState::HighHigh;
        snap.timestamp = 9999;

        buf.write(42, snap);
        buf.commit();

        auto r = buf.readTag(42);
        QCOMPARE(r.tagId, 42u);
        QCOMPARE(r.currentValue, 3.14f);
        QCOMPARE(r.setPoint, 5.0f);
        QCOMPARE(r.outputValue, 80.0f);
        QCOMPARE(r.quality, DataQuality::Bad);
        QCOMPARE(r.alarmstate, AlarmState::HighHigh);
        QCOMPARE(r.timestamp, 9999);
    }

    void testManyTags()
    {
        DoubleBuffer buf;
        const int TAG_COUNT = 500;
        for (int i = 0; i < TAG_COUNT; ++i) {
            DoubleBuffer::TagSnapshot snap;
            snap.tagId = static_cast<quint32>(i);
            snap.currentValue = static_cast<float>(i);
            buf.write(i, snap);
        }
        buf.commit();

        QCOMPARE(buf.readAll().size(), TAG_COUNT);
        QCOMPARE(buf.readTag(0).currentValue, 0.0f);
        QCOMPARE(buf.readTag(499).currentValue, 499.0f);
    }

    void testReadAllAfterSecondCommit()
    {
        DoubleBuffer buf;
        DoubleBuffer::TagSnapshot snap;
        snap.tagId = 1; snap.currentValue = 10.0f;
        buf.write(1, snap);
        buf.commit();

        snap.tagId = 2; snap.currentValue = 20.0f;
        buf.write(2, snap);
        buf.commit();

        auto result = buf.readAll();
        QCOMPARE(result.size(), 2);
    }

    void testQualityEnumMapping()
    {
        QCOMPARE(static_cast<int>(DataQuality::Good), 0);
        QCOMPARE(static_cast<int>(DataQuality::Bad), 1);
        QCOMPARE(static_cast<int>(DataQuality::Uncertain), 2);
    }
};

QTEST_MAIN(TestDoubleBuffer)
#include "TestDoubleBuffer.moc"
