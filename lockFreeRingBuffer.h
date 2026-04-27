#pragma once

#include <atomic>
#include <vector>
#include <cstddef>
#include <type_traits>
#include <mutex>

template<typename T, size_t Capacity = 8192>
class LockFreeRingBuffer {
    // T 必须保证异常安全，否则拷贝失败会导致当前线程破坏数据，
    // 从而导致队列对象状态坏掉。
    static_assert(std::is_nothrow_copy_assignable_v<T> || std::is_nothrow_move_assignable_v<T>,
        "T must be nothrow assignable to guarantee queue state consistency");
    static_assert((Capacity & (Capacity - 1)) == 0, "Capacity 必须是2的幂");
    static_assert(Capacity >= 2, "Capacity 不能小于2");

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

    /**
     * @brief 入队（拷贝版本，多生产者安全）
     */
    bool enqueue(const T& data)
    {
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
        // diff < 0 表示队列满
        return false;
    }

    /**
     * @brief 入队（移动版本，多生产者安全）
     */
    bool enqueue(T&& data)
    {
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

    /**
     * @brief 出队（单消费者，无锁）
     */
    bool dequeue(T& data)
    {
        size_t pos = m_dequeuePos.load(std::memory_order_relaxed);
        auto& slot = m_slots[pos & m_mask];
        size_t seq = slot.sequence.load(std::memory_order_acquire);
        intptr_t diff = static_cast<intptr_t>(seq) - static_cast<intptr_t>(pos);
        if (diff == 0) {
            data = std::move(slot.data);
            //标记槽位已被消费者读取，可以被生产者重新写入。
            slot.sequence.store(pos + 1 + m_mask, std::memory_order_release);
            m_dequeuePos.store(pos + 1, std::memory_order_relaxed);
            return true;
        }
        return false;
    }

    /**
     * @brief 批量出队
     * @return 实际出队数量
     */
    size_t dequeueBatch(std::vector<T>& output, size_t maxCount)
    {
        size_t count = 0;
        output.reserve(output.size() + maxCount);
        while (count < maxCount) {
            T item;
            if (!dequeue(item))
                break;
            output.push_back(std::move(item));
            count++;
        }
        return count;
    }

    bool empty() const
    {
        size_t enq = m_enqueuePos.load(std::memory_order_relaxed);
        size_t deq = m_dequeuePos.load(std::memory_order_relaxed);
        return enq == deq;
    }

    size_t size() const
    {
        size_t enq = m_enqueuePos.load(std::memory_order_relaxed);
        size_t dnq = m_dequeuePos.load(std::memory_order_relaxed);
        return (enq >= dnq) ? (enq - dnq) : 0;
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
