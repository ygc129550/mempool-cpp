#include "mempool.hpp"
#include <cstdlib>

// 计算实际块大小：取 max(sizeof(void*), alignof(std::max_align_t), size) 并向上对齐
size_t MemPool::align_block_size(size_t size) noexcept
{
    // 实现块大小对齐逻辑 实现优化：通过位运算实现向上取整，保证是align_size整数倍。&的左边保证已对齐的高位不变，未对齐的产生进位；然后后几位置空
    constexpr size_t align_size = alignof(std::max_align_t);

    if (size < sizeof(void *)) size = sizeof(void *);

    return (size + align_size - 1) & ~(align_size - 1);
}

// 构造函数：初始化成员，调用 align_block_size 处理 block_size，预分配 prealloc_cnt 个块
MemPool::MemPool(size_t block_size, size_t expand_step)
    : block_size_(MemPool::align_block_size(block_size))
    , expand_step_(expand_step > 0 ? expand_step : 1)
    , free_list_(nullptr)
    , big_blocks_(nullptr)
    , big_block_cnt_(0)
    , big_block_cap_(0)
{

}

// 析构函数：释放 big_blocks_ 中记录的所有大块内存
MemPool::~MemPool()
{
    // 遍历 big_blocks_ 逐一 free，最后释放 big_blocks_ 本身
    for (size_t i = 0; i < big_block_cnt_; i++)
    {
        free(big_blocks_[i]);
    }
    free(big_blocks_);
}

// 移动构造：接管源对象资源，将源对象置为空状态
MemPool::MemPool(MemPool&& other) noexcept
    : block_size_(other.block_size_)
    , expand_step_(other.expand_step_)
    , free_list_(other.free_list_)
    , big_blocks_(other.big_blocks_)
    , big_block_cnt_(other.big_block_cnt_)
    , big_block_cap_(other.big_block_cap_)
{
    // 将 other 的 free_list_、big_blocks_ 置 nullptr，计数置 0
    other.free_list_ = nullptr;
    other.big_blocks_ = nullptr;
    other.big_block_cap_ = 0;
    other.big_block_cnt_ = 0;
}

// 移动赋值：先释放自身资源，再接管源对象资源
MemPool& MemPool::operator=(MemPool&& other) noexcept
{
    // 处理自赋值
    if (this == &other) return *this;

    // 释放当前 big_blocks_ 中的所有大块
    for (size_t i = 0; i < big_block_cnt_; i++)
    {
        free(big_blocks_[i]);
    }
    free(big_blocks_);

    // 转移 other 的成员，将 other 置空
    block_size_ = other.block_size_;
    expand_step_ = other.expand_step_;
    free_list_ = other.free_list_;
    big_blocks_ = other.big_blocks_;
    big_block_cap_ = other.big_block_cap_;
    big_block_cnt_ = other.big_block_cnt_;

    other.free_list_ = nullptr;
    other.big_blocks_ = nullptr;
    other.big_block_cap_ = 0;
    other.big_block_cnt_ = 0;

    // return *this
    return *this;
}

// 扩容：申请 expand_step_ 个大块，切分为小块挂入空闲链表，并记录到 big_blocks_
void MemPool::expand()
{
    // malloc 一大块（block_size_ * expand_step_）
    // 将大块指针追加到 big_blocks_（必要时 realloc 扩容 big_blocks_）
    // 将大块切分为小块，逐个头插到 free_list_
    if (big_block_cap_ == big_block_cnt_)
    {
        size_t new_cap = big_block_cap_ > 0 ? big_block_cap_ * 2 : 4;
        void **new_big_blocks = static_cast<void**>(realloc(big_blocks_, new_cap * sizeof(void *)));
        if (!new_big_blocks) return;
        big_blocks_ = new_big_blocks;
        big_block_cap_ = new_cap;
    }

    void *bb = malloc(block_size_ * expand_step_);
    if (!bb) return;

    big_blocks_[big_block_cnt_++] = bb;

    for (size_t i = 0; i < expand_step_; i++)
    {
        void *small_block = static_cast<char *>(bb) + i * block_size_;
        *reinterpret_cast<void**>(small_block) = free_list_;
        free_list_ = small_block;
    }
}

// 分配：从空闲链表取一块，链表为空时先 expand
void* MemPool::alloc()
{
    // 若 free_list_ 为空，调用 expand()
    // 摘下空闲链表头节点并返回
    if (!free_list_)
    {
        expand();
    }
    if (!free_list_) return nullptr;

    void *ret = free_list_;
    free_list_ = *(reinterpret_cast<void **>(ret));

    return ret;
}

// 归还：将内存块头插回空闲链表
void MemPool::free(void* p)
{
    // 将 p 作为新头节点插入 free_list_
    if (!p) return;
    *reinterpret_cast<void **>(p) = free_list_;
    free_list_ = p;
}
