#pragma once
#include "export.h"
#include "TagDef.h"
#include <QObject>
#include <QTimer>
#include <QMutex>
#include <QVector>
#include <QDateTime>
#include <QMap>
#include <QPair>

/**
 * @brief ISA-18.2 报警 KPI 监控（Level 3）
 *
 * 滑动窗口统计，定时计算 KPI 并发出告警。
 *
 * ISA-18.2 推荐阈值：
 * - 10分钟报警率 ≤ 10
 * - 平均报警率 ≤ 2/操作员/小时
 * - 高峰报警率 ≤ 10
 * - 陈旧报警（未确认超30分钟）< 5
 */
class BUSINESS_EXPORT AlarmKpiMonitor : public QObject {
    Q_OBJECT
public:
    explicit AlarmKpiMonitor(QObject* parent = nullptr);

    /// 记录一次报警事件（带位号名，用于 top5 统计）
    void recordAlarm(const QString& tagName);

    /// 获取当前 KPI 快照
    /// @param totalActive  从外部传入的当前活跃报警数（AlarmEngine 提供）
    /// @param staleCount   从外部传入的陈旧报警数（未确认超30分钟）
    /// @param shelvedCount 从外部传入的屏蔽报警数
    AlarmKpiSnapshot snapshot(int totalActive = 0,
                              int staleCount = 0,
                              int shelvedCount = 0) const;

    /// 设置/获取 KPI 阈值
    void setThresholds(int rate10min, int staleMin, int peak10min);
    int  rateThreshold10min()   const { return m_rateThreshold10min; }
    int  staleThresholdMin()    const { return m_staleThresholdMin; }
    int  peakThreshold10min()   const { return m_peakThreshold10min; }

    /// 从外部注入当前报警状态（AlarmEngine 定时调用）
    void setExternalStats(int totalActive, int staleCount, int shelvedCount);

    /// 获取历史 KPI 快照（用于生成趋势图）
    QVector<AlarmKpiSnapshot> history() const { return m_history; }

signals:
    /// KPI 超阈值告警（ISA-18.2 要求报警系统自我监控）
    void kpiThresholdExceeded(const QString& metric, float value, float threshold);

    /// 定期 KPI 报告
    void kpiReport(const AlarmKpiSnapshot& snapshot);

private slots:
    void onTick();

private:
    struct AlarmEventEntry {
        qint64  timestamp = 0;  // unix sec
        QString tagName;
    };

    void pruneOldEvents();

    mutable QMutex m_mutex;

    // 报警事件列表（带位号名，用于滑动窗口 + top5 统计）
    QVector<AlarmEventEntry> m_events;

    // 历史快照（最近 24 小时，每分钟一个）
    QVector<AlarmKpiSnapshot> m_history;

    // 统计定时器
    QTimer* m_timer = nullptr;

    // ISA-18.2 推荐阈值
    int m_rateThreshold10min = 10;
    int m_staleThresholdMin  = 30;
    int m_peakThreshold10min = 10;

    // 外部注入的统计值（由 AlarmEngine 在每次 onTick 前设置）
    int m_externalTotalActive  = 0;
    int m_externalStaleCount   = 0;
    int m_externalShelvedCount = 0;
};
