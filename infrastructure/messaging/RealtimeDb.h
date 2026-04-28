#pragma once
#include <QObject>
#include <QHash>
#include <QMutex>
#include <functional>
#include "infrastructure/messaging/DoubleBuffer.h"
#include "domain/tag/TagManager.h"

class RealtimeDb : public QObject {
    Q_OBJECT
public:
    using ChangeCallback = std::function<void(quint32 tagId, float value, DataQuality quality)>;

    explicit RealtimeDb(QObject* parent = nullptr) : QObject(parent) {}

    void setDoubleBuffer(DoubleBuffer* buffer) { m_doubleBuffer = buffer; }
    DoubleBuffer* doubleBuffer() { return m_doubleBuffer; }

    void setTagManager(TagManager* mgr) { m_tagManager = mgr; }

    void updateValue(quint32 tagId, float value, DataQuality quality = DataQuality::Good) {
        if (!m_doubleBuffer) return;
        DoubleBuffer::TagSnapshot snap;
        snap.tagId = tagId;
        snap.currentValue = value;
        snap.quality = quality;
        snap.timestamp = QDateTime::currentMSecsSinceEpoch();
        m_doubleBuffer->write(tagId, snap);
        callCallbacks(tagId, value, quality);
        emit valueChanged(tagId, value);
        emit qualityChanged(tagId, quality);
    }

    void updateSetPoint(quint32 tagId, float sp) {
        if (!m_doubleBuffer) return;
        DoubleBuffer::TagSnapshot snap = m_doubleBuffer->readTag(tagId);
        snap.setPoint = sp;
        snap.timestamp = QDateTime::currentMSecsSinceEpoch();
        m_doubleBuffer->write(tagId, snap);
    }

    void updateOutput(quint32 tagId, float outVal) {
        if (!m_doubleBuffer) return;
        DoubleBuffer::TagSnapshot snap = m_doubleBuffer->readTag(tagId);
        snap.outputValue = outVal;
        snap.timestamp = QDateTime::currentMSecsSinceEpoch();
        m_doubleBuffer->write(tagId, snap);
    }

    void updateAlarmState(quint32 tagId, AlarmLimit limit) {
        if (!m_doubleBuffer) return;
        DoubleBuffer::TagSnapshot snap = m_doubleBuffer->readTag(tagId);
        snap.alarmState = limit;
        snap.timestamp = QDateTime::currentMSecsSinceEpoch();
        m_doubleBuffer->write(tagId, snap);
        emit alarmChanged(tagId, limit);
    }

    void updateQuality(quint32 tagId, DataQuality quality) {
        if (!m_doubleBuffer) return;
        DoubleBuffer::TagSnapshot snap = m_doubleBuffer->readTag(tagId);
        snap.quality = quality;
        snap.timestamp = QDateTime::currentMSecsSinceEpoch();
        m_doubleBuffer->write(tagId, snap);
        emit qualityChanged(tagId, quality);
    }

    void batchUpdate(const QHash<quint32, QPair<float, DataQuality>>& values) {
        if (!m_doubleBuffer) return;
        qint64 now = QDateTime::currentMSecsSinceEpoch();
        for (auto it = values.begin(); it != values.end(); ++it) {
            DoubleBuffer::TagSnapshot snap;
            snap.tagId = it.key();
            snap.currentValue = it.value().first;
            snap.quality = it.value().second;
            snap.timestamp = now;
            m_doubleBuffer->write(it.key(), snap);
            callCallbacks(it.key(), it.value().first, it.value().second);
            emit valueChanged(it.key(), it.value().first);
        }
    }

    void markAllBad() {
        if (!m_doubleBuffer || !m_tagManager) return;
        auto allTags = m_tagManager->getAllTags();
        qint64 now = QDateTime::currentMSecsSinceEpoch();
        for (const auto& tag : allTags) {
            DoubleBuffer::TagSnapshot snap = m_doubleBuffer->readTag(tag.tagId);
            snap.quality = DataQuality::Bad;
            snap.timestamp = now;
            m_doubleBuffer->write(tag.tagId, snap);
        }
    }

    void registerCallback(quint32 tagId, ChangeCallback cb) {
        QMutexLocker lock(&m_cbMutex);
        m_callbacks[tagId].append(std::move(cb));
    }

    void unregisterCallbacks(quint32 tagId) {
        QMutexLocker lock(&m_cbMutex);
        m_callbacks.remove(tagId);
    }

signals:
    void valueChanged(quint32 tagId, float value);
    void alarmChanged(quint32 tagId, AlarmLimit limit);
    void qualityChanged(quint32 tagId, DataQuality quality);

private:
    void callCallbacks(quint32 tagId, float value, DataQuality quality) {
        QMutexLocker lock(&m_cbMutex);
        auto it = m_callbacks.find(tagId);
        if (it == m_callbacks.end()) return;
        for (const auto& cb : *it) {
            if (cb) cb(tagId, value, quality);
        }
    }

    DoubleBuffer* m_doubleBuffer = nullptr;
    TagManager* m_tagManager = nullptr;
    QHash<quint32, QVector<ChangeCallback>> m_callbacks;
    mutable QMutex m_cbMutex;
};
