#pragma once
#include <cstddef>

class MemPool
{
public:
    // @brief 固定块大小内存池，单线程无锁
    // @param block_size 每一块内存大小（自动向上取整到 max(sizeof(void*), alignof(std::max_align_t))）
    // @param prealloc_cnt 初始预分配数量
    // @param expand_step 扩容时一次分配多少块
    explicit MemPool(size_t block_size, size_t expand_step);
    ~MemPool();

    // 禁止拷贝
    MemPool(const MemPool&) = delete;
    MemPool& operator=(const MemPool&) = delete;

    // 允许移动
    MemPool(MemPool&& other) noexcept;
    MemPool& operator=(MemPool&& other) noexcept;

    // @brief 从内存池取出一块内存
    void* alloc();

    // @brief 将内存块归还内存池
    void free(void* p);

private:
    size_t block_size_;
    size_t expand_step_;

    // 空闲链表头
    void* free_list_;

    // 保存所有malloc出来的大块，析构统一释放
    void** big_blocks_;
    size_t big_block_cnt_; //已分配的大块数量
    size_t big_block_cap_; //big_blocks_可容纳的大块数量

    // @brief 扩容：申请一大块，切分成小块挂进空闲链表
    void expand();

    // @brief 计算实际块大小（对齐到指针大小和max_align_t）
    static size_t align_block_size(size_t size) noexcept;
};
