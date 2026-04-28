#pragma once
#include <atomic>
#include <vector>
#include <mutex>
#include <cstddef>
#include <type_traits>
#include "infrastructure/messaging/IMessageBus.h"

template<typename T, size_t Capacity = 8192>
class LockFreeRingBuffer {
    static_assert(std::is_nothrow_copy_assignable_v<T> || std::is_nothrow_move_assignable_v<T>,
        "T must be nothrow assignable");
    static_assert((Capacity & (Capacity - 1)) == 0, "Capacity must be power of 2");
    static_assert(Capacity >= 2, "Capacity must be >= 2");

public:
    LockFreeRingBuffer()
        : m_enqueuePos(0), m_dequeuePos(0)
    {
        for (size_t i = 0; i < Capacity; i++) {
            m_slots[i].sequence.store(i, std::memory_order_release);
        }
    }
    LockFreeRingBuffer(const LockFreeRingBuffer&) = delete;
    LockFreeRingBuffer& operator=(const LockFreeRingBuffer&) = delete;

    bool enqueue(const T& data) {
        std::lock_guard<std::mutex> lock(m_enqueueMutex);
        size_t pos = m_enqueuePos.load(std::memory_order_relaxed);
        auto& slot = m_slots[pos & m_mask];
        size_t seq = slot.sequence.load(std::memory_order_acquire);
        intptr_t diff = static_cast<intptr_t>(seq) - static_cast<intptr_t>(pos);
        if (diff == 0) {
            slot.data = data;
            slot.sequence.store(pos + 1, std::memory_order_release);
            m_enqueuePos.store(pos + 1, std::memory_order_relaxed);
            return true;
        }
        return false;
    }

    bool enqueue(T&& data) {
        std::lock_guard<std::mutex> lock(m_enqueueMutex);
        size_t pos = m_enqueuePos.load(std::memory_order_relaxed);
        auto& slot = m_slots[pos & m_mask];
        size_t seq = slot.sequence.load(std::memory_order_acquire);
        intptr_t diff = static_cast<intptr_t>(seq) - static_cast<intptr_t>(pos);
        if (diff == 0) {
            slot.data = std::move(data);
            slot.sequence.store(pos + 1, std::memory_order_release);
            m_enqueuePos.store(pos + 1, std::memory_order_relaxed);
            return true;
        }
        return false;
    }

    bool dequeue(T& data) {
        size_t pos = m_dequeuePos.load(std::memory_order_relaxed);
        auto& slot = m_slots[pos & m_mask];
        size_t seq = slot.sequence.load(std::memory_order_acquire);
        intptr_t diff = static_cast<intptr_t>(seq) - static_cast<intptr_t>(pos);
        if (diff == 0) {
            data = std::move(slot.data);
            slot.sequence.store(pos + 1 + m_mask, std::memory_order_release);
            m_dequeuePos.store(pos + 1, std::memory_order_relaxed);
            return true;
        }
        return false;
    }

    size_t dequeueBatch(std::vector<T>& output, size_t maxCount) {
        size_t count = 0;
        output.reserve(output.size() + maxCount);
        while (count < maxCount) {
            T item;
            if (!dequeue(item)) break;
            output.push_back(std::move(item));
            count++;
        }
        return count;
    }

    bool empty() const {
        return m_enqueuePos.load(std::memory_order_relaxed) == m_dequeuePos.load(std::memory_order_relaxed);
    }

    size_t size() const {
        size_t enq = m_enqueuePos.load(std::memory_order_relaxed);
        size_t deq = m_dequeuePos.load(std::memory_order_relaxed);
        return (enq >= deq) ? (enq - deq) : 0;
    }

    static constexpr size_t capacity() { return Capacity; }

private:
    struct Slot {
        T data;
        std::atomic<size_t> sequence;
    };
    Slot m_slots[Capacity];
    alignas(64) std::mutex m_enqueueMutex;
    alignas(64) std::atomic<size_t> m_enqueuePos;
    alignas(64) std::atomic<size_t> m_dequeuePos;
    static constexpr size_t m_mask = Capacity - 1;
};

class RingBufMessageBus : public IMessageBus {
public:
    bool enqueue(const RawModbusData& data) override {
        return m_buffer.enqueue(data);
    }
    bool dequeue(RawModbusData& data) override {
        return m_buffer.dequeue(data);
    }
    size_t dequeueBatch(std::vector<RawModbusData>& output, size_t maxCount) override {
        return m_buffer.dequeueBatch(output, maxCount);
    }
    bool empty() const override { return m_buffer.empty(); }
    size_t size() const override { return m_buffer.size(); }

    LockFreeRingBuffer<RawModbusData, 8192>* ringBuffer() { return &m_buffer; }

private:
    LockFreeRingBuffer<RawModbusData, 8192> m_buffer;
};
