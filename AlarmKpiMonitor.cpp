#include "AlarmKpiMonitor.h"
#include "logger.h"
#include <QDateTime>
#include <QMap>
#include <algorithm>

AlarmKpiMonitor::AlarmKpiMonitor(QObject* parent)
    : QObject(parent)
{
    m_timer = new QTimer(this);
    m_timer->setInterval(60000);  // 每分钟计算一次
    connect(m_timer, &QTimer::timeout, this, &AlarmKpiMonitor::onTick);
    m_timer->start();
}

void AlarmKpiMonitor::recordAlarm(const QString& tagName)
{
    QMutexLocker lock(&m_mutex);
    AlarmEventEntry entry;
    entry.timestamp = QDateTime::currentSecsSinceEpoch();
    entry.tagName   = tagName;
    m_events.append(entry);
    pruneOldEvents();
}

void AlarmKpiMonitor::pruneOldEvents()
{
    qint64 cutoff = QDateTime::currentSecsSinceEpoch() - 7200;  // 保留2小时
    while (!m_events.isEmpty() && m_events.first().timestamp < cutoff) {
        m_events.removeFirst();
    }
}

AlarmKpiSnapshot AlarmKpiMonitor::snapshot(int totalActive,
                                            int staleCount,
                                            int shelvedCount) const
{
    QMutexLocker lock(&m_mutex);
    AlarmKpiSnapshot snap;
    snap.timestamp = QDateTime::currentSecsSinceEpoch();

    qint64 now = snap.timestamp;
    qint64 tenMinAgo = now - 600;
    qint64 oneHourAgo = now - 3600;

    // 10分钟报警数 + 收集1小时内 tagName 用于 top5
    QMap<QString, int> freqMap;
    int count10 = 0;
    int count1h = 0;

    for (const auto& e : m_events) {
        if (e.timestamp >= tenMinAgo) count10++;
        if (e.timestamp >= oneHourAgo) {
            count1h++;
            if (!e.tagName.isEmpty()) {
                freqMap[e.tagName]++;
            }
        }
    }
    snap.alarmCount10min = count10;
    snap.avgPerHour = count1h / 1.0f;

    // 计算峰值：把最近1小时按10分钟窗口滑动
    int peak = 0;
    for (qint64 windowStart = oneHourAgo; windowStart <= now - 600; windowStart += 60) {
        int winCount = 0;
        qint64 winEnd = windowStart + 600;
        for (const auto& e : m_events) {
            if (e.timestamp >= windowStart && e.timestamp < winEnd) winCount++;
        }
        if (winCount > peak) peak = winCount;
    }
    snap.peakCount10min = peak;

    // top5 最频繁报警位号
    if (!freqMap.isEmpty()) {
        QVector<QPair<int, QString>> sorted;
        sorted.reserve(freqMap.size());
        for (auto it = freqMap.cbegin(); it != freqMap.cend(); ++it) {
            sorted.append({it.value(), it.key()});
        }
        std::sort(sorted.begin(), sorted.end(),
                  [](const QPair<int, QString>& a, const QPair<int, QString>& b) {
                      return a.first > b.first;
                  });
        int n = qMin(sorted.size(), 5);
        for (int i = 0; i < n; ++i) {
            snap.top5Frequent.append(sorted[i].second);
        }
    }

    // 外部传入的状态数据
    snap.totalActive   = totalActive;
    snap.staleCount    = staleCount;
    snap.shelvedCount  = shelvedCount;

    // 检查阈值
    if (count10 > m_rateThreshold10min) {
        const_cast<AlarmKpiMonitor*>(this)->emit kpiThresholdExceeded(
            "10min_rate", count10, m_rateThreshold10min);
    }
    if (peak > m_peakThreshold10min) {
        const_cast<AlarmKpiMonitor*>(this)->emit kpiThresholdExceeded(
            "10min_peak", peak, m_peakThreshold10min);
    }

    return snap;
}

void AlarmKpiMonitor::setExternalStats(int totalActive, int staleCount, int shelvedCount)
{
    QMutexLocker lock(&m_mutex);
    m_externalTotalActive  = totalActive;
    m_externalStaleCount   = staleCount;
    m_externalShelvedCount = shelvedCount;
}

void AlarmKpiMonitor::onTick()
{
    pruneOldEvents();
    auto snap = snapshot(m_externalTotalActive,
                         m_externalStaleCount,
                         m_externalShelvedCount);
    m_history.append(snap);
    if (m_history.size() > 1440) m_history.removeFirst();  // 保留24小时
    emit kpiReport(snap);
}
