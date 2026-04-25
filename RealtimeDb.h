#pragma once
#include "export.h"
#include <QObject>
#include <QHash>
#include <QVector>
#include <QMutex>
#include <QMutexLocker>
#include <QStringList>
#include <functional>
#include "TagDef.h"
#include "TagConfigMgr.h"
#include "DoubleBuffer.h"
/**
 * @brief 实时内存数据库（改进版）
 *
 * 改进后的RealtimeDb有两个数据来源：
 * 1. 双缓冲区（DoubleBuffer）：DataParseThread写入，UI线程读取
 *    - 高频路径，无锁读取
 *    - 用于界面显示
 *
 * 2. 传统QHash存储：兼容旧接口
 *    - 低频路径，QReadWriteLock保护
 *    - 用于历史查询、报警记录等需要完整TagInfo的场景
 *    - 由syncFromDoubleBuffer()从双缓冲区同步
 *
 * 数据流：
 * DataParseThread ──▶ DoubleBuffer ──▶ UI线程读取（无锁）
 *                      │
 *                      └──▶ syncFromDoubleBuffer() ──▶ QHash（兼容旧接口）
 *
 * 踩坑经验：
 * - RealtimeDb不能直接用DoubleBuffer替代，因为很多地方需要完整TagInfo
 * - DoubleBuffer只存快照（TagSnapshot），不存静态属性（量程、报警限值等）
 * - 所以保留两套存储，通过定时同步保持一致
 */
class DATAENGINE_EXPORT RealtimeDb :public QObject {
	Q_OBJECT
public:
    static RealtimeDb& instance();
    // 设置DoubleBuffer引用（DataEngine初始化时调用）
    void setDoubleBuffer(DoubleBuffer* buffer) { m_doubleBuffer = buffer; }
    // ========== 位号配置管理（转发给TagConfigMgr） ==========
    bool addTag(const TagInfo& tag) {
        return TagConfigMgr::instance().addTag(tag);
    }
    bool removeTag(quint32 tagId) {
        return TagConfigMgr::instance().removeTag(tagId);
    }

    TagInfo getTag(quint32 tagId) const {
        return TagConfigMgr::instance().getTag(tagId);
    }

    TagInfo getTagByName(const QString& tagName) const {
        return TagConfigMgr::instance().getTagByName(tagName);
    }

    QList<TagInfo> getAllTags() const {
        return TagConfigMgr::instance().getAllTags();
    }

    QStringList getAllTagNames() const {
        return TagConfigMgr::instance().getAllTagNames();
    }

    int tagCount() const {
        return TagConfigMgr::instance().tagCount();
    }
    // ========== 实时数据更新（转发给DoubleBuffer） ==========
    void updateValue(quint32 tagId, float value, DataQuality quality = DataQuality::Good) {
        if (!m_doubleBuffer) return;
        DoubleBuffer::TagSnapshot snapshot;
        snapshot.tagId = tagId;
        snapshot.currentValue = value;
        snapshot.quality = quality;
        snapshot.timestamp = QDateTime::currentMSecsSinceEpoch();
        m_doubleBuffer->write(tagId,snapshot);
        emit valueChanged(tagId, value);

        // 执行回调
        executeCallbacks(tagId, value);
    }
    void updateSetPoint(quint32 tagId, float sp) {
        if (!m_doubleBuffer) return;

        auto snapshot = m_doubleBuffer->readTag(tagId);
        snapshot.setPoint = sp;
        m_doubleBuffer->write(tagId, snapshot);
    }
    void updateOutput(quint32 tagId, float out) {
        if (!m_doubleBuffer) return;

        auto snapshot = m_doubleBuffer->readTag(tagId);
        snapshot.outputValue = out;
        m_doubleBuffer->write(tagId, snapshot);
    }

    void updateAlarmState(quint32 tagId, AlarmLimit state) {
        if (!m_doubleBuffer) return;

        auto snapshot = m_doubleBuffer->readTag(tagId);
        snapshot.alarmstate = state;
        m_doubleBuffer->write(tagId, snapshot);
        emit alarmChanged(tagId, state);
    }

    void updateQuality(quint32 tagId, DataQuality quality) {
        if (!m_doubleBuffer) return;

        auto snapshot = m_doubleBuffer->readTag(tagId);
        snapshot.quality = quality;
        m_doubleBuffer->write(tagId, snapshot);
        emit qualityChanged(tagId, quality);
    }

    void batchUpdate(const QHash<quint32, QPair<float, DataQuality>>& updates) {
        if (!m_doubleBuffer) return;

        qint64 now = QDateTime::currentMSecsSinceEpoch();
        for (auto it = updates.constBegin(); it != updates.constEnd(); ++it) {
            DoubleBuffer::TagSnapshot snapshot;
            snapshot.tagId = it.key();
            snapshot.currentValue = it.value().first;
            snapshot.quality = it.value().second;
            snapshot.timestamp = now;
            m_doubleBuffer->write(it.key(), snapshot);
        }

        // 批量发射信号
        for (auto it = updates.constBegin(); it != updates.constEnd(); ++it) {
            emit valueChanged(it.key(), it.value().first);
        }
    }

    void markAllBad() {
        if (!m_doubleBuffer) return;

        auto allTags = TagConfigMgr::instance().getAllTags();
        for (const auto& tag : allTags) {
            auto snapshot = m_doubleBuffer->readTag(tag.tagId);
            snapshot.quality = DataQuality::Bad;
            m_doubleBuffer->write(tag.tagId, snapshot);
            emit qualityChanged(tag.tagId, DataQuality::Bad);
        }
    }




    // ========== 回调注册（用于图元绑定） ==========

    using ChangeCallback = std::function<void(quint32 tagId, float newValue)>;
    void registerCallback(quint32 tagId, ChangeCallback callback) {
        QMutexLocker lock(&m_callbackMutex);
        m_callbacks[tagId].append(std::move(callback));
    }
signals:
    void valueChanged(quint32 tagId, float newValue);
    void alarmChanged(quint32 tagId, AlarmLimit newState);
    void qualityChanged(quint32 tagId, DataQuality newQuality);
private:
    RealtimeDb() = default;
    ~RealtimeDb() override = default;
    RealtimeDb(const RealtimeDb&) = delete;
    RealtimeDb& operator=(const RealtimeDb&) = delete;

    void executeCallbacks(quint32 tagId, float value)
    {
        QMutexLocker lock(&m_callbackMutex);
        auto it = m_callbacks.find(tagId);
        if (it != m_callbacks.end())
        {
            for (const auto& cb : *it) {
                cb(tagId, value);
            }
        }
    }

    // DoubleBuffer引用（由DataEngine设置）
    DoubleBuffer* m_doubleBuffer = nullptr;

    // 回调表（图元绑定用）
    QHash<quint32, QVector<ChangeCallback>> m_callbacks;
    QMutex m_callbackMutex;
};
