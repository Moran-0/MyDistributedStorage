#pragma once
#include <vector>
#include <atomic>
#include <stdexcept>
#include <new>

/**
 * 基于 Dmitry Vyukov 经典设计的 Multi-Producer Multi-Consumer (MPMC) 无锁有界队列
 */

template <typename T>
class LockFreeQueue {
  public:
    // capacity 必须是 2 的幂次方，方便取模优化
    explicit LockFreeQueue(size_t capacity);
    bool Push(T const&); // 入队（生产者）
    bool Pop(T&);        // 出队（消费者）
  private:
    struct Cell {
        std::atomic<size_t> sequence;
        T data;
    };
    const size_t capacity_;
    const size_t mask_;
    std::vector<Cell> buffer_;
    // 强制缓存行对齐，防止“伪共享” (False Sharing) 破坏性能
    alignas(64) std::atomic<size_t> enqueue_pos_; // 入队位置
    alignas(64) std::atomic<size_t> dequeue_pos_; // 出队位置
};

template <typename T>
LockFreeQueue<T>::LockFreeQueue(size_t capacity) : capacity_(capacity), mask_(capacity - 1), buffer_(capacity) {
    if ((capacity_ & mask_) != 0) {
        throw std::invalid_argument("Capacity must be a power of 2");
    }
    // 初始化时无数据竞争，可采用宽松序列
    for (size_t i = 0; i < capacity_; ++i) {
        buffer_[i].sequence.store(i, std::memory_order_relaxed);
    }
    enqueue_pos_.store(0, std::memory_order_relaxed);
    dequeue_pos_.store(0, std::memory_order_relaxed);
}

template <typename T>
bool LockFreeQueue<T>::Push(T const& value) {
    size_t pos = enqueue_pos_.load(std::memory_order_relaxed);
    Cell* cell;
    // 循环尝试，类似自旋锁
    for (;;) {
        cell = &buffer_[(pos & mask_)];
        size_t seq = cell->sequence.load(std::memory_order_acquire); // 获取该槽位的序列号
        intptr_t diff = (intptr_t)seq - (intptr_t)pos;
        if (diff == 0) {
            if (enqueue_pos_.compare_exchange_weak(pos, pos + 1, std::memory_order_release)) {
                break;
            }
        } else if (diff < 0) {
            // 队列已满
            return false;
        } else {
            // pos读旧了
            pos = enqueue_pos_.load(std::memory_order_relaxed);
        }
    }
    cell->data = value;
    cell->sequence.store(pos + 1, std::memory_order_release);
    return true;
}

template <typename T>
bool LockFreeQueue<T>::Pop(T& value) {
    size_t pos = dequeue_pos_.load(std::memory_order_relaxed);
    Cell* cell;
    for (;;) {
        cell = &buffer_[(pos & mask_)];
        size_t seq = cell->sequence.load(std::memory_order_acquire);
        intptr_t diff = (intptr_t)seq - (intptr_t)(pos + 1);
        if (diff == 0) {
            if (dequeue_pos_.compare_exchange_weak(pos, pos + 1, std::memory_order_release)) {
                break;
            }
        } else if (diff < 0) {
            // 队列为空
            return false;
        } else {
            // 读旧了
            pos = dequeue_pos_.load(std::memory_order_relaxed);
        }
    }
    value = cell->data;
    cell->sequence.store(pos + mask_ + 1, std::memory_order_release);
    return true;
}
