#include "AlarmKpiMonitor.h"
#include <QMap>
#include <algorithm>

AlarmKpiMonitor::AlarmKpiMonitor(QObject* parent) : QObject(parent) {
    m_timer = new QTimer(this);
    m_timer->setInterval(60000);
    connect(m_timer, &QTimer::timeout, this, &AlarmKpiMonitor::onTick);
    m_timer->start();
}

void AlarmKpiMonitor::recordAlarm(const QString& tagName) {
    QMutexLocker lock(&m_mutex);
    m_events.append({QDateTime::currentSecsSinceEpoch(), tagName});
}

AlarmKpiSnapshot AlarmKpiMonitor::snapshot() const {
    QMutexLocker lock(&m_mutex);
    qint64 now = QDateTime::currentSecsSinceEpoch();
    qint64 tenMinAgo = now - 600;
    qint64 oneHourAgo = now - 3600;

    AlarmKpiSnapshot s;
    s.timestamp = now * 1000;

    int count10min = 0;
    QMap<QString, int> freqMap;
    for (const auto& e : m_events) {
        if (e.timestamp >= oneHourAgo) {
            freqMap[e.tagName]++;
            if (e.timestamp >= tenMinAgo) count10min++;
        }
    }
    s.alarmCount10min = count10min;
    s.avgPerHour = freqMap.size() > 0 ? (float)freqMap.size() : 0;
    s.peakCount10min = count10min;
    s.totalActive = m_externalTotalActive;
    s.staleCount = m_externalStaleCount;
    s.shelvedCount = m_externalShelvedCount;
    s.suppressedCount = 0;

    s.staleAlarmPercent = s.totalActive > 0 ? (s.staleCount * 100 / s.totalActive) : 0;

    // Top 5 frequent
    QVector<QPair<QString, int>> sorted;
    for (auto it = freqMap.begin(); it != freqMap.end(); ++it)
        sorted.append({it.key(), it.value()});
    std::sort(sorted.begin(), sorted.end(), [](const auto& a, const auto& b) { return a.second > b.second; });
    for (int i = 0; i < qMin(5, (int)sorted.size()); ++i)
        s.top5Frequent.append(sorted[i].first);

    // Health score (EEMUA 191)
    float score = 100.0f;
    if (s.alarmCount10min > m_rateThreshold10min) score -= 20;
    if (s.avgPerHour > 2) score -= 10;
    if (s.staleAlarmPercent > 5) score -= 10;
    if (s.peakCount10min > m_peakThreshold10min) score -= 15;

    s.systemHealthScore = qMax(0.0f, score);
    if (score >= 90) s.healthGrade = "A";
    else if (score >= 75) s.healthGrade = "B";
    else if (score >= 60) s.healthGrade = "C";
    else if (score >= 40) s.healthGrade = "D";
    else s.healthGrade = "F";

    return s;
}

void AlarmKpiMonitor::setThresholds(int rate10min, int staleMin, int peak10min) {
    m_rateThreshold10min = rate10min;
    m_staleThresholdMin = staleMin;
    m_peakThreshold10min = peak10min;
}

void AlarmKpiMonitor::setExternalStats(int totalActive, int staleCount, int shelvedCount) {
    QMutexLocker lock(&m_mutex);
    m_externalTotalActive = totalActive;
    m_externalStaleCount = staleCount;
    m_externalShelvedCount = shelvedCount;
}

QVector<QPair<quint32, int>> AlarmKpiMonitor::topFrequent(int topN) const {
    QMutexLocker lock(&m_mutex);
    QMap<QString, int> freqMap;
    qint64 oneHourAgo = QDateTime::currentSecsSinceEpoch() - 3600;
    for (const auto& e : m_events) {
        if (e.timestamp >= oneHourAgo) freqMap[e.tagName]++;
    }
    QVector<QPair<QString, int>> sorted;
    for (auto it = freqMap.begin(); it != freqMap.end(); ++it)
        sorted.append({it.key(), it.value()});
    std::sort(sorted.begin(), sorted.end(), [](const auto& a, const auto& b) { return a.second > b.second; });

    QVector<QPair<quint32, int>> result;
    for (int i = 0; i < qMin(topN, (int)sorted.size()); ++i) {
        result.append({0, sorted[i].second}); // tagId not tracked in simple impl
    }
    return result;
}

void AlarmKpiMonitor::onTick() {
    QMutexLocker lock(&m_mutex);
    pruneOldEvents();
    auto s = snapshot();
    m_history.append(s);
    if (m_history.size() > 1440) m_history.removeFirst();

    lock.unlock();
    emit kpiReport(s);
    if (s.alarmCount10min > m_rateThreshold10min) emit kpiThresholdExceeded("10minRate", s.alarmCount10min, m_rateThreshold10min);
}

void AlarmKpiMonitor::pruneOldEvents() {
    qint64 cutoff = QDateTime::currentSecsSinceEpoch() - 3600;
    while (!m_events.isEmpty() && m_events.first().timestamp < cutoff)
        m_events.removeFirst();
}
