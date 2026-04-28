#pragma once
#include <QThread>
#include <QAtomicInt>
#include <QMutex>
#include <QVector>
#include <QMap>
#include "infrastructure/messaging/DoubleBuffer.h"
#include "infrastructure/persistence/IHistoryRepo.h"

class HistorySampler : public QThread {
    Q_OBJECT
public:
    explicit HistorySampler(QObject* parent = nullptr);
    ~HistorySampler() override;

    void setDoubleBuffer(DoubleBuffer* buffer) { m_doubleBuffer = buffer; }
    void setHistoryRepo(IHistoryRepo* repo) { m_historyRepo = repo; }
    void setArchiveInterval(int seconds) { m_archiveIntervalSec = seconds; }
    void setSampleInterval(int ms) { m_sampleIntervalMs = ms; }
    void setCacheWindow(int seconds) { m_cacheWindowSec = qBound(60, seconds, 86400); }

    void stop();
    qint64 totalArchived() const { return m_totalArchived.loadRelaxed(); }
    qint64 totalFailed() const { return m_totalFailed.loadRelaxed(); }

    QVector<HistoryRecord> queryTrend(quint32 tagId, const QDateTime& start, const QDateTime& end, int maxPoints = 10000);
    QMap<quint32, QVector<HistoryRecord>> queryMultiTrend(const QVector<quint32>& tagIds,
        const QDateTime& start, const QDateTime& end, int maxPoints = 5000);

signals:
    void archiveCompleted(int recordCount, qint64 durationMs);
    void archiveFailed(const QString& error);

protected:
    void run() override;

private:
    void sampleData();
    bool doArchive();
    QVector<HistoryRecord> queryFromCache(quint32 tagId, const QDateTime& start, const QDateTime& end) const;
    void writeToRecentCache(quint32 tagId, const HistoryRecord& rec);

    struct TagHistoryRing {
        QVector<HistoryRecord> records;
        int head = 0;
        int count = 0;
        static constexpr int MAX_RECORDS = 1800;
    };

    DoubleBuffer* m_doubleBuffer = nullptr;
    IHistoryRepo* m_historyRepo = nullptr;

    QVector<HistoryRecord> m_cache;
    QMutex m_cacheMutex;
    qint64 m_firstRecordTime = 0;
    int m_archiveIntervalSec = 300;
    int m_sampleIntervalMs = 1000;

    QMap<quint32, TagHistoryRing> m_recentHistory;
    int m_cacheWindowSec = 1800;

    QAtomicInt m_running;
    QAtomicInt m_totalArchived;
    QAtomicInt m_totalFailed;
};
