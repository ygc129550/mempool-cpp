#pragma once
#include "mempool.hpp"
#include "object_pool.hpp"
#include <mutex>
#include <new>
#include <utility>

/// @brief 线程安全的 MemPool 封装
class ThreadSafeMemPool {
public:
    explicit ThreadSafeMemPool(size_t block_size, size_t expand_step)
        : pool_(block_size, expand_step) {}

    ThreadSafeMemPool(const ThreadSafeMemPool&) = delete;
    ThreadSafeMemPool& operator=(const ThreadSafeMemPool&) = delete;
    ThreadSafeMemPool(ThreadSafeMemPool&&) = delete;
    ThreadSafeMemPool& operator=(ThreadSafeMemPool&&) = delete;

    void* alloc() {
        std::lock_guard<std::mutex> lock(mutex_);
        return pool_.alloc();
    }

    void free(void* p) {
        std::lock_guard<std::mutex> lock(mutex_);
        pool_.free(p);
    }

private:
    MemPool pool_;
    std::mutex mutex_;
};

/// @brief 线程安全的 ObjectPool 封装
template<typename T>
class ThreadSafeObjectPool {
    static_assert(noexcept(std::declval<T>().~T()),
                  "ThreadSafeObjectPool requires T's destructor to be noexcept");
public:
    explicit ThreadSafeObjectPool(size_t expand_step)
        : pool_(expand_step) {}

    ThreadSafeObjectPool(const ThreadSafeObjectPool&) = delete;
    ThreadSafeObjectPool& operator=(const ThreadSafeObjectPool&) = delete;
    ThreadSafeObjectPool(ThreadSafeObjectPool&&) = delete;
    ThreadSafeObjectPool& operator=(ThreadSafeObjectPool&&) = delete;

    template<typename... Args>
    T* alloc(Args&&... args) {
        std::lock_guard<std::mutex> lock(mutex_);
        return pool_.alloc(std::forward<Args>(args)...);
    }

    void free(T* obj) {
        std::lock_guard<std::mutex> lock(mutex_);
        pool_.free(obj);
    }

private:
    ObjectPool<T> pool_;
    std::mutex mutex_;
};
