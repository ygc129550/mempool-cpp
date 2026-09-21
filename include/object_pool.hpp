#pragma once
#include "mempool.hpp"
#include <type_traits>
#include <utility>

template<typename T>
class ObjectPool
{
    static_assert(noexcept(std::declval<T>().~T()),
                  "ObjectPool requires T's destructor to be noexcept");

public:
    explicit ObjectPool(size_t expand_step)
        : pool_(sizeof(T), expand_step)
    {}

    // 禁止拷贝
    ObjectPool(const ObjectPool&) = delete;
    ObjectPool& operator=(const ObjectPool&) = delete;

    // 允许移动
    ObjectPool(ObjectPool&& other) noexcept = default;
    ObjectPool& operator=(ObjectPool&& other) noexcept = default;

    template<typename... Args>
    T* alloc(Args&&... args)
    {
        void* mem = pool_.alloc();
        return ::new(mem) T(std::forward<Args>(args)...);
    }

    void free(T* obj) noexcept
    {
        if (obj == nullptr)
            return;
        obj->~T();
        pool_.free(obj);
    }

private:
    MemPool pool_;
};
