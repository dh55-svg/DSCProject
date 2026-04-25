#pragma once
#include "core_global.h"
#include <QObject>
#include <QHash>
#include <QVector>
#include <QMutex>
#include <QElapsedTimer>
#include <QString>

class CORE_EXPORT PerformanceMonitor : public QObject {

	Q_OBJECT
public:
    static PerformanceMonitor& instance();

    /**
     * @brief 记录单个性能指标
     * @param name 指标名称
     * @param value 指标值
     */
    void recordMetric(const QString& name, double value);

    /**
     * @brief 记录操作延迟（毫秒）
     * @param operation 操作名称
     * @param durationMs 延迟时间（毫秒）
     */
    void recordLatency(const QString& operation, double durationMs);

    /**
     * @brief 记录计数器
     * @param name 计数器名称
     * @param count 增量值
     */
    void recordCount(const QString& name, int count = 1);

    /**
     * @brief 开始计时
     * @param timerName 计时器名称
     */
    void startTimer(const QString& timerName);

    /**
     * @brief 停止计时并返回耗时（毫秒）
     * @param timerName 计时器名称
     * @return 耗时（毫秒），如果计时器不存在则返回-1
     */
    double stopTimer(const QString& timerName);

    /**
     * @brief 获取指定指标的统计信息
     * @param name 指标名称
     * @return 统计信息（平均值、最大值、最小值、计数）
     */
    QHash<QString, double> getStatistics(const QString& name) const;

    /**
     * @brief 生成性能报告
     * @return 格式化的性能报告字符串
     */
    QString generateReport() const;

    /**
     * @brief 清除所有统计数据
     */
    void reset();
private:
	PerformanceMonitor();
	~PerformanceMonitor() override;
	PerformanceMonitor(const PerformanceMonitor&) = delete;
	PerformanceMonitor& operator=(const PerformanceMonitor&) = delete;


	struct MetricData {
		QVector<double> values;
		double sum = 0.0;
		double min = 0.0;
		double max = 0.0;
		int count = 0;
	};

	QHash<QString, MetricData> m_metrics;
	QHash<QString, QElapsedTimer> m_timers;
	mutable QMutex m_mutex;
};

#define PERF_START(name) PerformanceMonitor::instance().startTimer(name)
#define PERF_STOP(name) \
    PerformanceMonitor::instance().stopTimer(name)

#define PERF_RECORD_LATENCY(op, duration) \
    PerformanceMonitor::instance().recordLatency(op, duration)

#define PERF_RECORD_COUNT(name, count) \
    PerformanceMonitor::instance().recordCount(name, count)