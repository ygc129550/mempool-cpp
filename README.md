# mempool-cpp

一个轻量级、固定块大小的 C++ 通用内存池库，基于 C++17 标准实现。提供单线程零开销版本和线程安全封装版本。

## 特性

- **O(1) 分配与释放**：空闲链表头插法，无搜索
- **懒分配模式**：构造时不预分配内存，首次 `alloc` 才触发实际申请
- **自动对齐**：块大小自动向上取整到 `alignof(std::max_align_t)`，保证任意类型安全存储
- **RAII 管理**：析构时统一释放所有大块内存，无泄漏风险
- **移动语义支持**：单线程版本支持零成本所有权转移，兼容 STL 容器和函数返回
- **ObjectPool 模板**：封装 placement new / 显式析构，支持任意类型的构造参数转发
- **线程安全封装**：`ThreadSafeMemPool` / `ThreadSafeObjectPool<T>` 通过组合 + mutex 提供多线程安全，单线程版本保持零开销

## 目录结构

```
mempool-cpp/
├── CMakeLists.txt              # CMake 构建配置
├── build.sh                    # 一键编译脚本
├── include/
│   ├── mempool.hpp             # MemPool 对外头文件
│   ├── object_pool.hpp         # ObjectPool 模板头文件
│   └── thread_safe_pool.hpp    # 线程安全封装头文件
├── src/
│   └── mempool.cpp             # MemPool 实现
├── tests/
│   └── test_pool.cpp           # 单元测试（21 个用例）
└── README.md
```

## 快速开始

### 编译

```bash
./build.sh            # 一键编译
./build.sh clean      # 清理构建目录
./build.sh rebuild    # 清理后重新编译
```

也可手动使用 CMake：

```bash
cmake -S . -B build
cmake --build build
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

## API 概览

### MemPool（单线程）

| 接口 | 说明 |
|------|------|
| `MemPool(block_size, expand_step)` | 构造内存池，block_size 自动对齐，expand_step 保底为 1 |
| `void* alloc()` | 获取一块内存，空闲链表为空时自动扩容 |
| `void free(void* p)` | 归还内存，nullptr 安全 |
| 移动构造 / 移动赋值 | 转移所有权，源对象置空可安全析构 |
| 拷贝构造 / 拷贝赋值 | 已删除 |

### ObjectPool\<T\>（单线程）

| 接口 | 说明 |
|------|------|
| `ObjectPool(expand_step)` | 构造对象池，块大小自动设为 sizeof(T) |
| `T* alloc(args...)` | 分配并完美转发参数构造 T |
| `void free(T* obj)` | 析构对象并归还内存，nullptr 安全 |
| 移动构造 / 移动赋值 | 默认生成（委托给内部 MemPool） |
| 拷贝构造 / 拷贝赋值 | 已删除 |

> ⚠️ **约束**：`ObjectPool` 要求 T 的析构函数为 `noexcept`，否则编译期报错。

### ThreadSafeMemPool / ThreadSafeObjectPool\<T\>

| 接口 | 说明 |
|------|------|
| 构造参数 | 与对应的单线程版本相同 |
| `alloc()` / `free()` | 接口一致，内部自动加锁 |
| 拷贝 / 移动 | 全部删除（std::mutex 不可拷贝/移动） |

## 设计要点

- **空闲链表复用块头部**：空闲块的头部被用作 `void*` 指针存储下一个节点地址，零额外元数据开销
- **大块只增不减**：`big_blocks_` 数组记录所有 malloc 的大块，析构时统一释放，不做单块回收
- **倍增扩容**：`big_blocks_` 数组容量按 4→8→16→32… 倍增，均摊 O(1)
- **错误处理**：malloc/realloc 失败时安全返回 nullptr，不修改已有状态
- **线程安全分层**：单线程版本零同步开销；多线程版本通过组合 + mutex 封装，锁加在最底层

## 许可证

MIT License
