#include "mempool.hpp"
#include "object_pool.hpp"
#include <cassert>
#include <cstdio>
#include <set>
#include <vector>
#include <cstdint>
#include <cstring>

static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name)                                                             \
    static void test_##name();                                                 \
    struct Register_##name {                                                    \
        Register_##name() { run_test(#name, test_##name); }                     \
    } register_##name;                                                         \
    static void test_##name()

static void run_test(const char* name, void (*fn)()) {
    try {
        fn();
        ++tests_passed;
        std::printf("  PASS: %s\n", name);
    } catch (const std::exception& e) {
        ++tests_failed;
        std::printf("  FAIL: %s (%s)\n", name, e.what());
    } catch (...) {
        ++tests_failed;
        std::printf("  FAIL: %s (unknown exception)\n", name);
    }
}

// ==================== MemPool Tests ====================

TEST(alloc_returns_non_null) {
    MemPool pool(64, 16);
    void* p = pool.alloc();
    assert(p != nullptr);
    pool.free(p);
}

TEST(alloc_returns_unique_pointers) {
    MemPool pool(64, 16);
    std::set<void*> ptrs;
    for (int i = 0; i < 100; ++i) {
        void* p = pool.alloc();
        assert(p != nullptr);
        assert(ptrs.insert(p).second && "duplicate pointer returned");
    }
    for (auto* p : ptrs) pool.free(p);
}

TEST(free_and_reuse) {
    MemPool pool(64, 4);
    void* p1 = pool.alloc();
    pool.free(p1);
    void* p2 = pool.alloc();
    assert(p1 == p2 && "freed block should be reused");
    pool.free(p2);
}

TEST(zero_block_size_handled) {
    MemPool pool(0, 4);
    void* p = pool.alloc();
    assert(p != nullptr);
    pool.free(p);
}

TEST(lazy_allocation) {
    // 构造时不分配，首次 alloc 才触发 expand
    MemPool pool(64, 4);
    void* p = pool.alloc();
    assert(p != nullptr);
    pool.free(p);
}

TEST(multiple_expand) {
    MemPool pool(32, 4);
    std::vector<void*> ptrs;
    // 分配远超初始容量的块数，触发多次 expand
    for (int i = 0; i < 1000; ++i) {
        ptrs.push_back(pool.alloc());
        assert(ptrs.back() != nullptr);
    }
    for (auto* p : ptrs) pool.free(p);
}

TEST(free_nullptr_safe) {
    MemPool pool(64, 4);
    pool.free(nullptr); // 不应崩溃
}

TEST(move_constructor) {
    MemPool pool(64, 8);
    void* p = pool.alloc();
    MemPool moved(std::move(pool));
    // 移动后原对象应安全析构（不 double-free）
    // 新对象应能继续分配
    void* p2 = moved.alloc();
    assert(p2 != nullptr);
    moved.free(p);
    moved.free(p2);
}

TEST(move_assignment) {
    MemPool a(64, 8);
    MemPool b(32, 4);
    void* pa = a.alloc();
    b = std::move(a);
    // b 现在持有 a 的资源
    void* pb = b.alloc();
    assert(pb != nullptr);
    b.free(pa);
    b.free(pb);
}

// ==================== ObjectPool Tests ====================

struct Widget {
    int x;
    double y;
    Widget(int x, double y) : x(x), y(y) {}
};

TEST(object_pool_alloc_free) {
    ObjectPool<Widget> pool(8);
    Widget* w = pool.alloc(42, 3.14);
    assert(w != nullptr);
    assert(w->x == 42);
    assert(w->y == 3.14);
    pool.free(w);
}

TEST(object_pool_reuse_after_free) {
    ObjectPool<Widget> pool(4);
    Widget* w1 = pool.alloc(1, 1.0);
    pool.free(w1);
    Widget* w2 = pool.alloc(2, 2.0);
    // 复用同一块内存，地址相同
    assert(static_cast<void*>(w1) == static_cast<void*>(w2));
    assert(w2->x == 2 && w2->y == 2.0);
    pool.free(w2);
}

TEST(object_pool_free_nullptr_safe) {
    ObjectPool<Widget> pool(4);
    pool.free(nullptr); // 不应崩溃
}

int main() {
    std::printf("Running mempool-cpp tests...\n\n");
    // 测试通过静态注册自动运行
    std::printf("\nResults: %d passed, %d failed\n", tests_passed, tests_failed);
    return tests_failed > 0 ? 1 : 0;
}

// ==================== Alignment Tests ====================

TEST(alloc_returns_aligned_pointer) {
    MemPool pool(1, 8); // 最小块大小，验证自动对齐
    constexpr size_t alignment = alignof(std::max_align_t);
    for (int i = 0; i < 50; ++i) {
        void* p = pool.alloc();
        assert(p != nullptr);
        assert(reinterpret_cast<uintptr_t>(p) % alignment == 0
               && "pointer not aligned to max_align_t");
    }
}

TEST(various_block_sizes_aligned) {
    // 不同原始块大小都应返回对齐指针
    size_t sizes[] = {1, 3, 7, 9, 15, 17, 33, 63, 100};
    constexpr size_t alignment = alignof(std::max_align_t);
    for (size_t sz : sizes) {
        MemPool pool(sz, 4);
        void* p = pool.alloc();
        assert(p != nullptr);
        assert(reinterpret_cast<uintptr_t>(p) % alignment == 0);
        pool.free(p);
    }
}

// ==================== Write Safety Tests ====================

TEST(write_and_read_back) {
    MemPool pool(64, 4);
    void* p = pool.alloc();
    assert(p != nullptr);
    // 写入整个块
    std::memset(p, 0xAB, 64);
    // 读回验证
    unsigned char* bytes = static_cast<unsigned char*>(p);
    for (int i = 0; i < 64; ++i) {
        assert(bytes[i] == 0xAB && "data corrupted after write");
    }
    pool.free(p);
}

TEST(adjacent_blocks_no_overlap) {
    MemPool pool(32, 8);
    void* a = pool.alloc();
    void* b = pool.alloc();
    assert(a != nullptr && b != nullptr);
    // 向两个相邻块写入不同模式
    std::memset(a, 0xAA, 32);
    std::memset(b, 0xBB, 32);
    // 验证互不干扰
    unsigned char* pa = static_cast<unsigned char*>(a);
    unsigned char* pb = static_cast<unsigned char*>(b);
    for (int i = 0; i < 32; ++i) {
        assert(pa[i] == 0xAA && "block a corrupted by block b write");
        assert(pb[i] == 0xBB && "block b corrupted by block a write");
    }
    pool.free(a);
    pool.free(b);
}

TEST(free_then_realloc_preserves_new_data) {
    MemPool pool(64, 4);
    void* p = pool.alloc();
    std::memset(p, 0xFF, 64);
    pool.free(p);
    // 重新分配同一块，写入新数据
    void* p2 = pool.alloc();
    assert(p == p2);
    std::memset(p2, 0x00, 64);
    unsigned char* bytes = static_cast<unsigned char*>(p2);
    for (int i = 0; i < 64; ++i) {
        assert(bytes[i] == 0x00 && "old data leaked after realloc");
    }
    pool.free(p2);
}

// ==================== ObjectPool Move Tests ====================

TEST(object_pool_move_constructor) {
    ObjectPool<Widget> pool(8);
    Widget* w = pool.alloc(10, 2.0);
    assert(w->x == 10);
    // 移动构造
    ObjectPool<Widget> moved(std::move(pool));
    // 原池不应再使用，新池应能继续分配和释放
    Widget* w2 = moved.alloc(20, 3.0);
    assert(w2->x == 20);
    moved.free(w);
    moved.free(w2);
}

TEST(object_pool_move_assignment) {
    ObjectPool<Widget> a(8);
    ObjectPool<Widget> b(4);
    Widget* wa = a.alloc(100, 1.0);
    // 移动赋值：b 接管 a 的资源
    b = std::move(a);
    Widget* wb = b.alloc(200, 2.0);
    assert(wb->x == 200);
    b.free(wa);
    b.free(wb);
}

// ==================== Thread Safety Tests ====================
#include "thread_safe_pool.hpp"
#include <thread>
#include <atomic>

TEST(thread_safe_mempool_concurrent_alloc_free) {
    ThreadSafeMemPool pool(64, 16);
    std::atomic<int> success_count{0};
    constexpr int threads = 8;
    constexpr int ops_per_thread = 500;

    auto worker = [&]() {
        for (int i = 0; i < ops_per_thread; ++i) {
            void* p = pool.alloc();
            if (p) {
                std::memset(p, 0xCC, 64);
                pool.free(p);
                ++success_count;
            }
        }
    };

    std::thread ts[threads];
    for (int i = 0; i < threads; ++i) ts[i] = std::thread(worker);
    for (int i = 0; i < threads; ++i) ts[i].join();

    assert(success_count == threads * ops_per_thread
           && "some alloc/free failed under contention");
}

TEST(thread_safe_object_pool_concurrent) {
    ThreadSafeObjectPool<Widget> pool(8);
    std::atomic<int> success_count{0};
    constexpr int threads = 8;
    constexpr int ops_per_thread = 200;

    auto worker = [&](int id) {
        for (int i = 0; i < ops_per_thread; ++i) {
            Widget* w = pool.alloc(id, static_cast<double>(i));
            if (w) {
                assert(w->x == id);
                pool.free(w);
                ++success_count;
            }
        }
    };

    std::thread ts[threads];
    for (int i = 0; i < threads; ++i) ts[i] = std::thread(worker, i);
    for (int i = 0; i < threads; ++i) ts[i].join();

    assert(success_count == threads * ops_per_thread
           && "concurrent ObjectPool operations failed");
}
