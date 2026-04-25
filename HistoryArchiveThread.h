#pragma once
#include "export.h"
#include <QThread>
#include <QAtomicInt>
#include <QMutex>
#include <QVector>
#include <QDateTime>
#include <QMap>

#include "TagDef.h"
#include "DoubleBuffer.h"
#include "DatabaseManager.h"

/**
 * @brief 历史归档线程（精简版）
 *
 * 精简后职责：
 * - 从DoubleBuffer采样数据
 * - 内存缓存管理（5分钟窗口）
 * - 调用DatabaseManager写入MySQL
 *
 * 架构：
 * ┌──────────────┐     ┌─────────────────────┐     ┌────────────────┐
 * │ DoubleBuffer │────▶│ HistoryArchiveThread│────▶│DatabaseManager │
 * │ (双缓冲区)   │读取 │ 5分钟缓存+批量写入  │     │   MySQL连接    │
 * └──────────────┘     └─────────────────────┘     └────────────────┘
 *
 * 精简要点：
 * - 移除MySQL连接管理，由DatabaseManager统一管理
 * - 移除表创建逻辑，由DatabaseManager统一管理
 * - 只负责采样、缓存、调用DatabaseManager
 */
class BUSINESS_EXPORT HistoryArchiveThread : public QThread {
    Q_OBJECT

public:
    static HistoryArchiveThread& instance();

    explicit HistoryArchiveThread(QObject* parent = nullptr);
    ~HistoryArchiveThread() override;

    // 设置双缓冲区（数据源）
    void setDoubleBuffer(DoubleBuffer* buffer);

    // 设置归档间隔（秒），默认5分钟
    void setArchiveInterval(int seconds);

    // 设置采样间隔（毫秒），默认1秒
    void setSampleInterval(int ms);

    // 停止线程
    void stop();

    // 统计信息
    qint64 totalArchived() const { return m_totalArchived.loadRelaxed(); }
    qint64 totalFailed() const { return m_totalFailed.loadRelaxed(); }

    // 查询历史趋势（转发给DatabaseManager）
    QVector<HistoryRecord> queryTrend(quint32 tagId,
        const QDateTime& startTime,
        const QDateTime& endTime,
        int maxPoints = 10000);

    // 查询多个位号历史趋势
    QMap<quint32, QVector<HistoryRecord>> queryMultiTrend(
        const QVector<quint32>& tagIds,
        const QDateTime& startTime,
        const QDateTime& endTime,
        int maxPoints = 5000);

signals:
    void archiveCompleted(int recordCount, qint64 durationMs);
    void archiveFailed(const QString& error);

protected:
    void run() override;

private:
    HistoryArchiveThread(const HistoryArchiveThread&) = delete;
    HistoryArchiveThread& operator=(const HistoryArchiveThread&) = delete;

    // 从双缓冲区采样数据
    void sampleData();

    // 执行归档（调用DatabaseManager写入）
    bool doArchive();

    // 缓存的历史数据记录
    struct ArchiveRecord {
        quint32 tagId;
        double value;
        qint64 timestamp;
        quint8 quality;
    };

    QVector<ArchiveRecord> m_cache;
    QMutex m_cacheMutex;
    qint64 m_firstRecordTime = 0;
    int m_archiveIntervalSec = 300;
    int m_sampleIntervalMs = 1000;

    DoubleBuffer* m_doubleBuffer = nullptr;
    QAtomicInt m_running;
    QAtomicInt m_totalArchived;
    QAtomicInt m_totalFailed;
};

