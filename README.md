# mempool-cpp

> 个人学习项目：从零实现一个固定块大小的 C++ 通用内存池，记录设计决策与权衡思考。

## 项目定位

这不是一个生产级分配器，而是为了深入理解以下问题而写的学习代码：

- 内存池相比 malloc 到底快在哪里？
- 空闲链表如何做到 O(1) 分配/释放？
- 移动语义在资源管理类中如何保证安全？
- 单线程版本和线程安全版本应该如何分层？
- `std::allocator` 和底层内存池是什么关系？

所有设计选择都优先服务于"可理解性"，而非极致性能。

## 核心设计思路

### 1. 空闲链表的节点存在哪里

这是内存池最巧妙的设计：**空闲块的头部本身就是链表节点**。

```
空闲时:  [next_ptr (void*) | 剩余空间...]  → 下一个空闲块
分配后:  [用户数据........................]  → next_ptr 被用户数据覆盖
归还时:  [next_ptr (void*) | 剩余空间...]  → 重新写入指针，插回链表
```

这意味着元数据开销为零——不需要额外的数组或哈希表来追踪哪些块是空闲的。前提是 `block_size >= sizeof(void*)`，这由 `align_block_size` 强制保证。

### 2. 大块只增不减的设计取舍

每次 `expand()` 申请一大块连续内存（`block_size × expand_step`），切分为小块挂入链表。这些大块的指针记录在 `big_blocks_` 数组中，**只在析构时统一释放，运行期间从不回收单个大块**。

为什么不回收？因为要判断一个大块是否"全部空闲"，需要为每个大块维护引用计数或位图，`alloc/free` 时还需要定位小块属于哪个大块（O(n) 或额外哈希表）。这些开销违背了内存池 O(1) 的核心承诺。

**权衡**：牺牲了内存归还 OS 的能力，换取了分配/释放的常数时间保证。对于生命周期与进程绑定的池化场景，这是合理的；对于需要动态缩容的场景，应使用其他方案。

### 3. 对齐策略

`align_block_size` 将用户请求的块大小向上取整到 `alignof(std::max_align_t)`，同时保证不小于 `sizeof(void*)`。

```cpp
constexpr size_t alignment = alignof(std::max_align_t);
if (size < sizeof(void*)) size = sizeof(void*);
return (size + alignment - 1) & ~(alignment - 1);
```

两个保底各有用途：
- `>= sizeof(void*)`：保证空闲链表节点能放下一个指针
- `>= alignof(max_align_t)`：保证 placement new 任意标准类型都不会触发未定义行为

位运算 `(x + n - 1) & ~(n - 1)` 仅在 n 是 2 的幂时正确，C++ 标准保证 `alignof` 结果一定是 2 的幂。

### 4. 懒分配 vs 预分配

构造函数不分配任何内存，首次 `alloc()` 发现链表为空时才触发 `expand()`。

**理由**：避免构造时就占用内存，让池的生命周期更灵活。如果用户构造了池但从未使用，零开销。代价是首次 alloc 有一次额外的 expand 延迟，但对于池化场景这通常可以忽略。

### 5. big_blocks_ 数组的倍增扩容

`big_blocks_` 本身是一个动态数组，记录所有大块的指针。扩容采用倍增策略（4→8→16→32…），初始容量为 4。

**为什么是 4**：`4 × sizeof(void*) = 32 字节`，不到一个缓存行，对绝大多数轻量场景足够，避免前几次 expand 频繁 realloc。

**为什么先 realloc 再 malloc 大块**：如果反过来，malloc 成功后 realloc 失败，新大块已分配但无处记录，造成内存泄漏。先扩容数组则不存在这个问题——realloc 失败时还没 malloc 大块，零副作用。

### 6. 移动语义的资源契约

移动构造和移动赋值的核心原则：**被移走的对象必须可安全析构**。

```cpp
// 移动后源对象置空
other.free_list_ = nullptr;
other.big_blocks_ = nullptr;
other.big_block_cnt_ = 0;
other.big_block_cap_ = 0;
```

只清空持有堆资源的指针和计数，值类型（`block_size_`、`expand_step_`）无需置零——它们不持有资源，析构时不会 free 任何东西。

移动赋值比移动构造多两步：自赋值检查（`this == &other`）和释放自身已有资源。缺少任一步都会导致 double-free 或内存泄漏。

### 7. 线程安全：粗粒度大锁

`ThreadSafeMemPool` 在每次 `alloc/free` 调用时锁住整个池子：

```cpp
void* alloc() {
    std::lock_guard<std::mutex> lock(mutex_);
    return pool_.alloc();
}
```

**为什么不用细粒度锁**：
- 分段锁需要按地址哈希分桶，增加 alloc 路径的计算开销
- 读写锁不适用——alloc 和 free 都是写操作
- TLS + 全局回退是最优解，但实现复杂度高，超出学习项目范围

**当前方案的代价**：高竞争下 mutex 成为瓶颈，多线程吞吐可能低于直接使用 malloc。但对于低竞争或中等竞争场景，池化的 O(1) 优势仍然显著超过锁开销。

**设计原则**：锁加在最底层（MemPool 层），而非 ObjectPool 层。ObjectPool 的线程安全版本直接组合 `ObjectPool + mutex`，而非依赖 ThreadSafeMemPool，因为 ObjectPool 内部的 MemPool 是私有成员，外部无法注入。未来可通过依赖注入重构为更干净的分层。

### 8. ObjectPool 的 noexcept 析构约束

```cpp
static_assert(noexcept(std::declval<T>().~T()),
              "ObjectPool requires T's destructor to be noexcept");
```

`free()` 标记为 `noexcept`，内部直接调用 `obj->~T()`。如果 T 的析构抛异常，`noexcept` 函数内会直接 `std::terminate()`，用户得不到任何有意义的错误信息。`static_assert` 把这个检查提前到编译期，让用户在写代码时就能看到清晰的报错。

`std::declval<T>()` 在不构造对象的前提下获得类型引用，仅用于编译期类型推导，零运行时开销。

## 目录结构

```
mempool-cpp/
├── CMakeLists.txt              # CMake 构建配置（C++17，静态库 + CTest）
├── build.sh                    # 一键编译脚本（build/clean/rebuild）
├── include/
│   ├── mempool.hpp             # MemPool 类声明
│   ├── object_pool.hpp         # ObjectPool<T> 模板
│   └── thread_safe_pool.hpp    # ThreadSafeMemPool + ThreadSafeObjectPool<T>
├── src/
│   └── mempool.cpp             # MemPool 8 个成员函数实现
├── tests/
│   └── test_pool.cpp           # 21 个单元测试（自注册宏 + 全局构造执行）
└── README.md
```

## 快速开始

### 编译与测试

```bash
./build.sh            # 一键编译
./build.sh clean      # 清理构建目录
./build.sh rebuild    # 清理后重新编译
```

### 运行测试

```bash
cd build && ctest --verbose
```

### 基本用法

```cpp
#include "mempool.hpp"
#include "object_pool.hpp"
#include "thread_safe_pool.hpp"

// ===== 单线程原始内存池 =====
MemPool pool(64, 16);       // 64字节块，每次扩容16块
void* mem = pool.alloc();
pool.free(mem);

// ===== 单线程对象池 =====
struct Point {
    int x, y;
    Point(int x, int y) : x(x), y(y) {}
};
ObjectPool<Point> point_pool(16);
Point* p = point_pool.alloc(3, 4);   // placement new
point_pool.free(p);                   // 显式析构 + 归还内存

// ===== 多线程安全内存池 =====
ThreadSafeMemPool safe_pool(64, 16);
void* m = safe_pool.alloc();          // 内部自动加锁
safe_pool.free(m);

// ===== 多线程安全对象池 =====
ThreadSafeObjectPool<Point> safe_point_pool(16);
Point* sp = safe_point_pool.alloc(1, 2);
safe_point_pool.free(sp);
```

## API 速查

### MemPool（单线程，支持移动）

| 接口 | 说明 |
|------|------|
| `MemPool(block_size, expand_step)` | block_size 自动对齐，expand_step 保底为 1 |
| `void* alloc()` | 链表空时自动 expand，malloc 失败返回 nullptr |
| `void free(void* p)` | 头插回链表，nullptr 安全 |

### ObjectPool\<T\>（单线程，支持移动）

| 接口 | 说明 |
|------|------|
| `ObjectPool(expand_step)` | 构造对象池，块大小自动设为 sizeof(T) |
| `T* alloc(args...)` | 分配并完美转发参数构造 T |
| `void free(T* obj)` | 析构对象并归还内存，nullptr 安全 |
| 移动构造 / 移动赋值 | 默认生成（委托给内部 MemPool） |
| 拷贝构造 / 拷贝赋值 | 已删除 |

> ⚠️ **约束**：`ObjectPool` 要求 T 的析构函数为 `noexcept`，否则编译期报错。

### ThreadSafeMemPool / ThreadSafeObjectPool\<T\>

接口与单线程版本一致，内部 mutex 保护。**不可拷贝、不可移动**（std::mutex 限制）。

## 已知局限

- **不支持批量连续分配**：alloc 返回的小块不保证物理连续，需要连续内存应使用 malloc/vector
- **大块不回收**：内存只增不减，不适合需要动态缩容的场景
- **粗粒度锁**：高竞争下性能退化，生产环境应考虑 TLS + 全局回退或分段锁
- **无调试支持**：没有 double-free 检测、越界检查等安全特性
