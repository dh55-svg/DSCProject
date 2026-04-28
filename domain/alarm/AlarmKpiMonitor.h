#pragma once
#include <QObject>
#include <QTimer>
#include <QMutex>
#include <QVector>
#include <QMap>
#include <QPair>
#include "domain/alarm/AlarmEvent.h"

class AlarmKpiMonitor : public QObject {
    Q_OBJECT
public:
    explicit AlarmKpiMonitor(QObject* parent = nullptr);
    void recordAlarm(const QString& tagName);
    AlarmKpiSnapshot snapshot() const;

    void setThresholds(int rate10min, int staleMin, int peak10min);
    int rateThreshold10min() const { return m_rateThreshold10min; }
    int staleThresholdMin() const { return m_staleThresholdMin; }
    int peakThreshold10min() const { return m_peakThreshold10min; }

    void setExternalStats(int totalActive, int staleCount, int shelvedCount);
    QVector<AlarmKpiSnapshot> history() const { return m_history; }
    QVector<QPair<quint32, int>> topFrequent(int topN = 5) const;

signals:
    void kpiThresholdExceeded(const QString& metric, float value, float threshold);
    void kpiReport(const AlarmKpiSnapshot& snapshot);

private slots:
    void onTick();

private:
    struct AlarmEventEntry { qint64 timestamp = 0; QString tagName; };
    void pruneOldEvents();

    mutable QMutex m_mutex;
    QVector<AlarmEventEntry> m_events;
    QVector<AlarmKpiSnapshot> m_history;
    QTimer* m_timer = nullptr;
    int m_rateThreshold10min = 10;
    int m_staleThresholdMin = 30;
    int m_peakThreshold10min = 10;
    int m_externalTotalActive = 0;
    int m_externalStaleCount = 0;
    int m_externalShelvedCount = 0;
};
