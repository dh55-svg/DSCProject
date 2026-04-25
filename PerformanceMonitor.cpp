#include "PerformanceMonitor.h"
#include "logger.h"
#include <QDateTime>
#include <algorithm>
#include <cmath>

PerformanceMonitor& PerformanceMonitor::instance()
{
    static PerformanceMonitor instance;
    return instance;
}

void PerformanceMonitor::recordMetric(const QString& name, double value)
{
    QMutexLocker lock(&m_mutex);
    auto& data = m_metrics[name];
    data.values.append(value);
    data.sum += value;
    data.count++;

    if (data.count == 1 || data.min > value)
        data.min = value;
    if (data.count == 1 || data.max < value)
        data.max = value;

    // 限制采样数，防止内存膨胀
    if (data.values.size() > 10000)
    {
        double old = data.values.takeFirst();
        data.sum -= old;

        if (!data.values.isEmpty())
        {
            auto [minIt, maxIt] = std::minmax_element(data.values.begin(), data.values.end());
            data.min = *minIt;
            data.max = *maxIt;
        }
    }
}

void PerformanceMonitor::recordLatency(const QString& operation, double durationMs)
{
    recordMetric(operation + "_latency", durationMs);
}

void PerformanceMonitor::recordCount(const QString& name, int count)
{
    QMutexLocker lock(&m_mutex);
    auto& data = m_metrics[name + "_count"];
    data.sum += count;
    data.count += count;
}

void PerformanceMonitor::startTimer(const QString& timerName)
{
    QMutexLocker lock(&m_mutex);
    m_timers[timerName].start();
}

double PerformanceMonitor::stopTimer(const QString& timerName)
{
    QMutexLocker lock(&m_mutex);
    auto it = m_timers.find(timerName);
    if (it == m_timers.end())
        return -1.0;

    qint64 elapsed = it->elapsed();
    m_timers.erase(it);

    double ms = static_cast<double>(elapsed);
    recordLatency(timerName, ms);
    return ms;
}

QHash<QString, double> PerformanceMonitor::getStatistics(const QString& name) const
{
    QMutexLocker lock(&m_mutex);
    QHash<QString, double> stats;
    auto it = m_metrics.find(name);
    if (it != m_metrics.end())
    {
        const auto& data = it.value();
        stats["count"] = data.count;
        stats["sum"] = data.sum;
        stats["min"] = data.min;
        stats["max"] = data.max;
        stats["avg"] = data.count > 0 ? data.sum / data.count : 0.0;
    }
    return stats;
}

QString PerformanceMonitor::generateReport() const
{
    QMutexLocker lock(&m_mutex);
    QString report;
    report += QString("=== 性能监视报告 ===\n");
    report += QString("指标数: %1\n\n").arg(m_metrics.size());

    for (auto it = m_metrics.constBegin(); it != m_metrics.constEnd(); ++it)
    {
        const auto& data = it.value();
        if (data.count > 0)
        {
            report += QString("%1: 次数=%2, 平均=%3, 最小=%4, 最大=%5\n")
                .arg(it.key())
                .arg(data.count)
                .arg(data.sum / data.count, 0, 'f', 2)
                .arg(data.min, 0, 'f', 2)
                .arg(data.max, 0, 'f', 2);
        }
    }
    return report;
}

void PerformanceMonitor::reset()
{
    QMutexLocker lock(&m_mutex);
    m_metrics.clear();
    m_timers.clear();
}

PerformanceMonitor::PerformanceMonitor()
{
}

PerformanceMonitor::~PerformanceMonitor()
{
}
