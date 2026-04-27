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

    // 查询历史趋势（优先内存缓存，未命中则走数据库）
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

    // 设置内存缓存窗口大小（秒），默认1800秒=30分钟
    void setCacheWindow(int seconds) { m_cacheWindowSec = qBound(60, seconds, 86400); }

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

    // 从内存缓存查询历史数据
    QVector<HistoryRecord> queryFromCache(quint32 tagId,
        const QDateTime& startTime, const QDateTime& endTime) const;

    // 将采样数据写入内存环形缓存
    void writeToRecentCache(quint32 tagId, const ArchiveRecord& rec);

    QVector<ArchiveRecord> m_cache;
    QMutex m_cacheMutex;
    qint64 m_firstRecordTime = 0;
    int m_archiveIntervalSec = 300;
    int m_sampleIntervalMs = 1000;

    // 内存最近历史缓存（环形缓冲区，用于趋势图快速响应）
    // 每个位号最多保留 cacheWindowSec / sampleIntervalMs 条记录
    struct TagHistoryRing {
        QVector<HistoryRecord> records;  // 环形存储
        int head = 0;                     // 写入位置
        int count = 0;                    // 有效记录数
        static constexpr int MAX_RECORDS = 1800;  // 最多缓存1800条（30分钟1秒采样）
    };
    QMap<quint32, TagHistoryRing> m_recentHistory;
    int m_cacheWindowSec = 1800;          // 缓存窗口大小（秒），30分钟

    DoubleBuffer* m_doubleBuffer = nullptr;
    QAtomicInt m_running;
    QAtomicInt m_totalArchived;
    QAtomicInt m_totalFailed;
};

