#include "HistorySampler.h"
#include <QDateTime>
#include <QElapsedTimer>

HistorySampler::HistorySampler(QObject* parent) : QThread(parent) {
    m_running.storeRelaxed(0);
    m_totalArchived.storeRelaxed(0);
    m_totalFailed.storeRelaxed(0);
}

HistorySampler::~HistorySampler() { stop(); }

void HistorySampler::stop() {
    m_running.storeRelaxed(0);
    if (isRunning()) { quit(); wait(3000); }
}

void HistorySampler::run() {
    m_running.storeRelaxed(1);
    m_firstRecordTime = 0;

    while (m_running.loadRelaxed()) {
        sampleData();

        if (m_firstRecordTime > 0) {
            qint64 now = QDateTime::currentMSecsSinceEpoch();
            if (now - m_firstRecordTime >= static_cast<qint64>(m_archiveIntervalSec) * 1000) {
                doArchive();
            }
        }
        QThread::msleep(m_sampleIntervalMs);
    }
}

void HistorySampler::sampleData() {
    if (!m_doubleBuffer) return;

    auto snap = m_doubleBuffer->readAll();
    if (!snap || snap->empty()) return;

    qint64 now = QDateTime::currentMSecsSinceEpoch();
    QMutexLocker lock(&m_cacheMutex);

    for (const auto& [tagId, ts] : *snap) {
        HistoryRecord rec;
        rec.tagId = tagId;
        rec.value = ts.currentValue;
        rec.quality = static_cast<int>(ts.quality);
        rec.timestamp = now;
        m_cache.append(rec);
        writeToRecentCache(tagId, rec);
    }

    if (m_firstRecordTime == 0 && !m_cache.isEmpty()) {
        m_firstRecordTime = now;
    }
}

bool HistorySampler::doArchive() {
    if (!m_historyRepo) return false;

    QVector<HistoryRecord> records;
    {
        QMutexLocker lock(&m_cacheMutex);
        records = std::move(m_cache);
        m_cache.clear();
        m_firstRecordTime = 0;
    }

    if (records.isEmpty()) return true;

    QElapsedTimer timer;
    timer.start();
    try {
        m_historyRepo->batchInsert(records);
        m_totalArchived.storeRelaxed(m_totalArchived.loadRelaxed() + records.size());
        emit archiveCompleted(static_cast<int>(records.size()), timer.elapsed());
        return true;
    } catch (...) {
        m_totalFailed.storeRelaxed(m_totalFailed.loadRelaxed() + records.size());
        emit archiveFailed("Database write failed");
        return false;
    }
}

void HistorySampler::writeToRecentCache(quint32 tagId, const HistoryRecord& rec) {
    auto& ring = m_recentHistory[tagId];
    if (ring.records.isEmpty()) {
        ring.records.resize(TagHistoryRing::MAX_RECORDS);
    }
    ring.records[ring.head] = rec;
    ring.head = (ring.head + 1) % TagHistoryRing::MAX_RECORDS;
    if (ring.count < TagHistoryRing::MAX_RECORDS) ring.count++;
}

QVector<HistoryRecord> HistorySampler::queryFromCache(quint32 tagId, const QDateTime& start, const QDateTime& end) const {
    QVector<HistoryRecord> result;
    auto it = m_recentHistory.find(tagId);
    if (it == m_recentHistory.end()) return result;

    qint64 startMs = start.toMSecsSinceEpoch();
    qint64 endMs = end.toMSecsSinceEpoch();
    const auto& ring = it.value();

    for (int i = 0; i < ring.count; ++i) {
        int idx = (ring.head - 1 - i + TagHistoryRing::MAX_RECORDS) % TagHistoryRing::MAX_RECORDS;
        const auto& rec = ring.records[idx];
        if (rec.timestamp >= startMs && rec.timestamp <= endMs) {
            result.prepend(rec);
        }
    }
    return result;
}

QVector<HistoryRecord> HistorySampler::queryTrend(quint32 tagId, const QDateTime& start, const QDateTime& end, int maxPoints) {
    auto cached = queryFromCache(tagId, start, end);
    if (!cached.isEmpty()) return cached.mid(0, maxPoints);

    if (m_historyRepo) return m_historyRepo->query(tagId, start.toMSecsSinceEpoch(), end.toMSecsSinceEpoch(), maxPoints);
    return {};
}

QMap<quint32, QVector<HistoryRecord>> HistorySampler::queryMultiTrend(
    const QVector<quint32>& tagIds, const QDateTime& start, const QDateTime& end, int maxPoints) {
    QMap<quint32, QVector<HistoryRecord>> result;
    for (auto tagId : tagIds) {
        result[tagId] = queryTrend(tagId, start, end, maxPoints);
    }
    return result;
}
