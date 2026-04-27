#include "AlarmKpiMonitor.h"
#include "logger.h"
#include <QMap>
#include <QPair>
#include <algorithm>

// ============================================================
// AlarmKpiMonitor - ISA-18.2 / EEMUA 191 商业化 KPI 监控
// ============================================================

AlarmKpiMonitor::AlarmKpiMonitor(QObject* parent)
    : QObject(parent)
{
    // 每分钟计算一次 KPI 快照
    m_timer = new QTimer(this);
    m_timer->setInterval(60000);
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

    // 清理超过1小时的旧事件（滑动窗口只需要10分钟，但保留1小时用于小时级统计）
    pruneOldEvents();
}

AlarmKpiSnapshot AlarmKpiMonitor::snapshot(int totalActive,
                                            int staleCount,
                                            int shelvedCount) const
{
    QMutexLocker lock(&m_mutex);

    AlarmKpiSnapshot snap;
    snap.timestamp = QDateTime::currentMSecsSinceEpoch();

    qint64 now = QDateTime::currentSecsSinceEpoch();
    qint64 window10min = now - 600;   // 10分钟窗口
    qint64 window1hour = now - 3600;  // 1小时窗口

    // === 10分钟报警率 ===
    int count10min = 0;
    for (const auto& ev : m_events) {
        if (ev.timestamp >= window10min) {
            count10min++;
        }
    }
    snap.alarmCount10min = count10min;

    // === 平均报警率/小时 ===
    int count1hour = 0;
    for (const auto& ev : m_events) {
        if (ev.timestamp >= window1hour) {
            count1hour++;
        }
    }

    // 如果不足1小时，按实际时间比例计算
    if (!m_events.isEmpty()) {
        qint64 oldestEvent = m_events.first().timestamp;
        float elapsedHours = qMax(1.0f, static_cast<float>(now - oldestEvent) / 3600.0f);
        if (elapsedHours > 1.0f) elapsedHours = 1.0f;
        snap.avgPerHour = static_cast<float>(count1hour) / qMax(elapsedHours, 0.01f);
    }

    // === 高峰报警率（历史快照中的最大10分钟率） ===
    snap.peakCount10min = count10min;
    for (const auto& histSnap : m_history) {
        if (histSnap.alarmCount10min > snap.peakCount10min) {
            snap.peakCount10min = histSnap.alarmCount10min;
        }
    }

    // === 外部注入的统计值 ===
    snap.totalActive    = m_externalTotalActive;
    snap.staleCount     = m_externalStaleCount;
    snap.shelvedCount   = m_externalShelvedCount;

    // === 陈旧报警百分比 ===
    if (totalActive > 0) {
        snap.staleAlarmPercent = static_cast<int>(staleCount * 100 / totalActive);
    }

    // === Top5 最频繁报警位号 ===
    QMap<QString, int> tagCounts;
    for (const auto& ev : m_events) {
        if (ev.timestamp >= window1hour) {
            tagCounts[ev.tagName]++;
        }
    }
    QList<QPair<int, QString>> sorted;
    for (auto it = tagCounts.begin(); it != tagCounts.end(); ++it) {
        sorted.append(qMakePair(it.value(), it.key()));
    }
    std::sort(sorted.begin(), sorted.end(), [](const auto& a, const auto& b) {
        return a.first > b.first;
    });
    for (int i = 0; i < qMin(5, sorted.size()); ++i) {
        snap.top5Frequent.append(QString("%1(%2次)").arg(sorted[i].second).arg(sorted[i].first));
    }

    // === 系统健康度评分（EEMUA 191 基准） ===
    float score = 100.0f;

    // 10分钟报警率评分（EEMUA 191: ≤10 为可管理）
    if (count10min > 10) {
        score -= qMin(30.0f, (count10min - 10) * 2.0f);
    }

    // 平均报警率评分（EEMUA 191: ≤2/小时 为可接受）
    if (snap.avgPerHour > 2.0f) {
        score -= qMin(25.0f, (snap.avgPerHour - 2.0f) * 5.0f);
    }

    // 陈旧报警评分（<5% 为可接受）
    if (snap.staleAlarmPercent > 5) {
        score -= qMin(20.0f, static_cast<float>(snap.staleAlarmPercent - 5) * 2.0f);
    }

    // 屏蔽报警评分（过多屏蔽说明报警配置有问题）
    if (shelvedCount > 0 && totalActive > 0) {
        float shelvedRatio = static_cast<float>(shelvedCount) / (totalActive + shelvedCount);
        if (shelvedRatio > 0.1f) {
            score -= qMin(15.0f, (shelvedRatio - 0.1f) * 100.0f);
        }
    }

    // 高峰报警评分
    if (snap.peakCount10min > 20) {
        score -= qMin(10.0f, static_cast<float>(snap.peakCount10min - 20) * 0.5f);
    }

    snap.systemHealthScore = qMax(0.0f, score);

    // 健康等级
    if (snap.systemHealthScore >= 90)      snap.healthGrade = "A";
    else if (snap.systemHealthScore >= 75)  snap.healthGrade = "B";
    else if (snap.systemHealthScore >= 60)  snap.healthGrade = "C";
    else if (snap.systemHealthScore >= 40)  snap.healthGrade = "D";
    else                                    snap.healthGrade = "F";

    return snap;
}

void AlarmKpiMonitor::setThresholds(int rate10min, int staleMin, int peak10min)
{
    QMutexLocker lock(&m_mutex);
    m_rateThreshold10min = rate10min;
    m_staleThresholdMin  = staleMin;
    m_peakThreshold10min = peak10min;
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
    QMutexLocker lock(&m_mutex);

    // 清理旧事件
    pruneOldEvents();

    // 生成 KPI 快照
    AlarmKpiSnapshot snap = snapshot(m_externalTotalActive,
                                      m_externalStaleCount,
                                      m_externalShelvedCount);

    // 保存到历史（最近24小时，每分钟一个快照）
    m_history.append(snap);
    if (m_history.size() > 1440) {
        m_history.removeFirst();
    }

    // 检查阈值并发出告警
    if (snap.alarmCount10min > m_rateThreshold10min) {
        lock.unlock();
        emit kpiThresholdExceeded("alarmCount10min",
                                   static_cast<float>(snap.alarmCount10min),
                                   static_cast<float>(m_rateThreshold10min));
        LOG_WARN("AlarmKpiMonitor",
                 QString("KPI超阈值: 10分钟报警率=%1 (阈值=%2)")
                     .arg(snap.alarmCount10min).arg(m_rateThreshold10min));
        lock.relock();
    }

    if (snap.staleAlarmPercent > 5) {
        lock.unlock();
        emit kpiThresholdExceeded("staleAlarmPercent",
                                   static_cast<float>(snap.staleAlarmPercent),
                                   5.0f);
        LOG_WARN("AlarmKpiMonitor",
                 QString("KPI超阈值: 陈旧报警百分比=%1%% (阈值=5%%)")
                     .arg(snap.staleAlarmPercent));
        lock.relock();
    }

    if (snap.avgPerHour > 2.0f) {
        lock.unlock();
        emit kpiThresholdExceeded("avgPerHour",
                                   snap.avgPerHour,
                                   2.0f);
        LOG_WARN("AlarmKpiMonitor",
                 QString("KPI超阈值: 平均报警率=%.1f/小时 (阈值=2.0)")
                     .arg(snap.avgPerHour));
        lock.relock();
    }

    // 发出定期 KPI 报告
    lock.unlock();
    emit kpiReport(snap);
}

void AlarmKpiMonitor::pruneOldEvents()
{
    // 清理超过1小时的旧事件
    qint64 cutoff = QDateTime::currentSecsSinceEpoch() - 3600;
    while (!m_events.isEmpty() && m_events.first().timestamp < cutoff) {
        m_events.removeFirst();
    }
}
