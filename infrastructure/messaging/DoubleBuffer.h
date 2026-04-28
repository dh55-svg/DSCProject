#pragma once
#include <atomic>
#include <vector>
#include <unordered_map>
#include <memory>
#include "domain/common/DataQuality.h"
#include "domain/common/AlarmLimit.h"

class DoubleBuffer {
public:
    struct TagSnapshot {
        quint32 tagId = 0;
        float currentValue = 0.0f;
        float setPoint = 0.0f;
        float outputValue = 0.0f;
        AlarmLimit alarmState = AlarmLimit::Normal;
        DataQuality quality = DataQuality::Good;
        qint64 timestamp = 0;
    };

    using SnapshotMap = std::unordered_map<quint32, TagSnapshot>;
    using ImmutableSnapshot = std::shared_ptr<const SnapshotMap>;

    DoubleBuffer() {
        m_readOnlySnapshot = std::make_shared<const SnapshotMap>();
        m_writeBuffer.reserve(512);
        for (int i = 0; i < POOL_SIZE; ++i) {
            auto map = std::make_unique<SnapshotMap>();
            map->reserve(512);
            m_pool[i].store(map.release(), std::memory_order_relaxed);
        }
    }

    DoubleBuffer(const DoubleBuffer&) = delete;
    DoubleBuffer& operator=(const DoubleBuffer&) = delete;

    ~DoubleBuffer() {
        for (int i = 0; i < POOL_SIZE; ++i) {
            delete m_pool[i].load(std::memory_order_relaxed);
        }
    }

    void write(quint32 tagId, const TagSnapshot& snapshot) {
        m_writeBuffer[tagId] = snapshot;
    }

    void writeBatch(const std::vector<TagSnapshot>& snapshots) {
        for (const auto& snap : snapshots) {
            m_writeBuffer[snap.tagId] = snap;
        }
    }

    void commit() {
        SnapshotMap* map = acquireFromPool();
        *map = std::move(m_writeBuffer);

        auto deleter = [this](const SnapshotMap* m) {
            const_cast<SnapshotMap*>(m)->clear();
            returnToPool(const_cast<SnapshotMap*>(m));
        };

        ImmutableSnapshot newSnap(map, deleter);
        std::atomic_store(&m_readOnlySnapshot, std::move(newSnap));

        m_writeBuffer.clear();
        m_writeBuffer.reserve(512);
    }

    ImmutableSnapshot readAll() const {
        return std::atomic_load(&m_readOnlySnapshot);
    }

    TagSnapshot readTag(quint32 tagId) const {
        auto it = readAll();
        auto tag = it->find(tagId);
        if (tag != it->end()) return tag->second;
        return TagSnapshot{};
    }

    size_t size() const { return readAll()->size(); }

private:
    static constexpr int POOL_SIZE = 4;

    SnapshotMap* acquireFromPool() {
        for (int i = 0; i < POOL_SIZE; ++i) {
            SnapshotMap* expected = m_pool[i].load(std::memory_order_relaxed);
            if (expected && m_pool[i].compare_exchange_weak(expected, nullptr,
                    std::memory_order_acquire, std::memory_order_relaxed)) {
                return expected;
            }
        }
        auto map = new SnapshotMap();
        map->reserve(512);
        return map;
    }

    void returnToPool(SnapshotMap* map) {
        for (int i = 0; i < POOL_SIZE; ++i) {
            SnapshotMap* expected = nullptr;
            if (m_pool[i].compare_exchange_weak(expected, map,
                    std::memory_order_release, std::memory_order_relaxed)) {
                return;
            }
        }
        delete map;
    }

    SnapshotMap m_writeBuffer;
    std::atomic<SnapshotMap*> m_pool[POOL_SIZE] = {};
    mutable ImmutableSnapshot m_readOnlySnapshot;
};
