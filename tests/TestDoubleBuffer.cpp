#include <QTest>
#include <QFile>
#include <QTextStream>
#include <vector>
#include "../DoubleBuffer.h"

class TestDoubleBuffer : public QObject {
    Q_OBJECT

private slots:
    void testEmptyBuffer()
    {
        DoubleBuffer buf;
        auto snap = buf.readAll();
        QVERIFY(snap->empty());
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
        snap.alarmstate = AlarmLimit::Normal;
        snap.timestamp = 1000;

        buf.write(1, snap);
        QVERIFY(buf.readAll()->empty());

        buf.commit();
        auto result = buf.readAll();
        QCOMPARE(result->size(), static_cast<size_t>(1));
        auto it = result->find(1);
        QVERIFY(it != result->end());
        QCOMPARE(it->second.currentValue, 42.5f);
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

        QCOMPARE(buf.size(), static_cast<size_t>(3));
        QCOMPARE(buf.readTag(2).currentValue, 20.0f);
        QCOMPARE(buf.readTag(99).tagId, 0u);
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
        snap.alarmstate = AlarmLimit::HighHigh;
        snap.timestamp = 9999;

        buf.write(42, snap);
        buf.commit();

        auto r = buf.readTag(42);
        QCOMPARE(r.tagId, 42u);
        QCOMPARE(r.currentValue, 3.14f);
        QCOMPARE(r.setPoint, 5.0f);
        QCOMPARE(r.outputValue, 80.0f);
        QCOMPARE(r.quality, DataQuality::Bad);
        QCOMPARE(r.alarmstate, AlarmLimit::HighHigh);
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

        QCOMPARE(buf.size(), static_cast<size_t>(TAG_COUNT));
        QCOMPARE(buf.readTag(0).currentValue, 0.0f);
        QCOMPARE(buf.readTag(499).currentValue, 499.0f);
    }

    void testWriteBatch()
    {
        DoubleBuffer buf;
        std::vector<DoubleBuffer::TagSnapshot> batch;
        for (int i = 0; i < 10; ++i) {
            DoubleBuffer::TagSnapshot s;
            s.tagId = static_cast<quint32>(i);
            s.currentValue = static_cast<float>(i * 10);
            batch.push_back(s);
        }
        buf.writeBatch(batch);
        buf.commit();

        QCOMPARE(buf.size(), static_cast<size_t>(10));
        QCOMPARE(buf.readTag(5).currentValue, 50.0f);
    }
};

int main(int argc, char *argv[])
{
    QFile logFile("TestDoubleBuffer_results.txt");
    logFile.open(QIODevice::WriteOnly | QIODevice::Text);
    QTextStream log(&logFile);

    TestDoubleBuffer t;
    int result = QTest::qExec(&t, argc, argv);

    QFile resultFile("test_exit_codes.txt");
    resultFile.open(QIODevice::Append | QIODevice::Text);
    QTextStream rs(&resultFile);
    rs << "TestDoubleBuffer: " << (result == 0 ? "PASS" : "FAIL") << " (rc=" << result << ")\n";

    return result;
}

#include "TestDoubleBuffer.moc"
