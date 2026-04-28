#pragma once
#include <QObject>
#include <QHash>
#include <QMutex>
#include <QDateTime>
#include <QStringList>
#include <QPair>
#include <QElapsedTimer>

class PerformanceMonitor : public QObject {
    Q_OBJECT
public:
    struct MetricStats {
        int    count = 0;
        double sum = 0.0;
        double min = 1e18;
        double max = -1e18;
        double avg = 0.0;
    };

    void recordMetric(const QString& name, double value) {
        QMutexLocker lock(&m_mutex);
        auto& s = m_metrics[name];
        s.count++;
        s.sum += value;
        if (value < s.min) s.min = value;
        if (value > s.max) s.max = value;
        s.avg = s.sum / s.count;
        if (s.count > 10000) { s.count = 10000; }
    }

    void recordLatency(const QString& operation, double durationMs) {
        recordMetric(QString("latency.%1").arg(operation), durationMs);
    }

    void recordCount(const QString& name, int count) {
        QMutexLocker lock(&m_mutex);
        auto& s = m_metrics[name];
        s.count = count;
    }

    void startTimer(const QString& name) {
        QMutexLocker lock(&m_mutex);
        m_timers[name].start();
    }

    double stopTimer(const QString& name) {
        QMutexLocker lock(&m_mutex);
        auto it = m_timers.find(name);
        if (it == m_timers.end()) return 0.0;
        double elapsed = it->elapsed();
        recordLatency(name, elapsed);
        m_timers.erase(it);
        return elapsed;
    }

    MetricStats getStatistics(const QString& name) const {
        QMutexLocker lock(&m_mutex);
        return m_metrics.value(name);
    }

    QString generateReport() const {
        QMutexLocker lock(&m_mutex);
        QString report;
        report += "=== Performance Report ===\n";
        report += QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss") + "\n\n";
        for (auto it = m_metrics.begin(); it != m_metrics.end(); ++it) {
            const auto& s = it.value();
            report += QString("%1: count=%2 avg=%3 min=%4 max=%5\n")
                .arg(it.key()).arg(s.count).arg(s.avg, 0, 'f', 3).arg(s.min, 0, 'f', 3).arg(s.max, 0, 'f', 3);
        }
        return report;
    }

    void reset() {
        QMutexLocker lock(&m_mutex);
        m_metrics.clear();
        m_timers.clear();
    }

    QStringList metricNames() const {
        QMutexLocker lock(&m_mutex);
        return m_metrics.keys();
    }

private:
    mutable QMutex m_mutex;
    QHash<QString, MetricStats> m_metrics;
    QHash<QString, QElapsedTimer> m_timers;
};

// Convenience macros
#define PERF_START(name) PerformanceMonitor::instance().startTimer(name)
#define PERF_STOP(name)  PerformanceMonitor::instance().stopTimer(name)
#define PERF_RECORD_LATENCY(op, ms) PerformanceMonitor::instance().recordLatency(op, ms)
#define PERF_RECORD_COUNT(name, n) PerformanceMonitor::instance().recordCount(name, n)
