# xlog 性能优化方案

## 当前架构优势

1. **预分配环形缓冲区** - log_record 队列在初始化时一次性分配，避免运行时 malloc/free
2. **缓存行对齐** - log_record 64字节对齐，避免 false sharing
3. **无锁队列** - 使用 atomic 操作，生产者无需等待

## 优化点分析

### 1. 线程本地缓存 (Thread-Local Cache)

**问题**: 每次 `xlog_log` 都要竞争 `write_idx`，高并发时 `atomic_fetch_add` 会导致缓存行争用。

**方案**: 引入线程本地批量提交
```c
_Thread_local struct {
    log_record batch[8];  // 本地批量缓存
    int count;
} tls_batch;
```

当本地缓存满或刷新时，一次性获取多个槽位，减少原子操作次数。

### 2. 时间戳缓存优化

**已实现**: 秒级时间戳缓存（localtime_r 结果缓存）

**可进一步优化**:
- 使用 `CLOCK_MONOTONIC_COARSE` 替代 `CLOCK_REALTIME`（更快但精度较低）
- 批量获取时间戳

### 3. 字符串处理优化

**问题**: `strlen` 和字符串拷贝开销

**方案**:
- 对于已知长度的字符串，避免重复 `strlen`
- 使用 `memcpy` 替代逐字符拷贝

### 4. 格式化输出优化

**已实现**: 预编译格式模式，避免运行时解析

**可进一步优化**:
- 使用整数快速转字符串（已实现 SIMD 版本）
- 批量 I/O（已实现 batch_writer）

### 5. 内存分配器选择

#### 方案 A: 对象池 (推荐用于嵌入式)
```c
// 完全避免动态分配，适合资源受限环境
static log_record g_record_pool[MAX_RECORDS];
```
**优点**: 零分配开销，确定性延迟
**缺点**: 内存使用固定，灵活性差

#### 方案 B: mimalloc/jemalloc (推荐用于高性能服务器)
**优点**: 
- 多线程优化，减少锁竞争
- 更好的内存碎片处理
- mimalloc 延迟比 glibc malloc 低 ~7x

**集成方式**:
```cmake
# CMakeLists.txt
option(XLOG_USE_MIMALLOC "Use mimalloc for memory allocation" OFF)
if(XLOG_USE_MIMALLOC)
    find_package(mimalloc REQUIRED)
    target_link_libraries(xlog PRIVATE mimalloc)
    target_compile_definitions(xlog PRIVATE XLOG_USE_MIMALLOC)
endif()
```

#### 方案 C: 当前方案 (预分配队列)
**当前已实现**: 环形缓冲区预分配，运行时零分配
**适用场景**: 大多数场景，平衡性能和灵活性

### 6. CPU 亲和性优化

将后端线程绑定到特定 CPU，减少上下文切换：
```c
#ifdef __linux__
cpu_set_t cpuset;
CPU_ZERO(&cpuset);
CPU_SET(cpu_id, &cpuset);
pthread_setaffinity_np(thread, sizeof(cpu_set_t), &cpuset);
#endif
```

### 7. 批量刷新优化

**已实现**: batch_writer 批量写入

**可进一步优化**:
- 使用 `writev` 系统调用一次写入多个 iovec
- 调整批量大小基于负载动态调整

## 性能测试基准

当前基准 (单线程):
- 异步日志: ~2-3M logs/sec
- 同步日志: ~500K logs/sec

优化目标:
- 异步日志: ~5M+ logs/sec
- 同步日志: ~1M logs/sec

## 推荐优化优先级

1. **高优先级**: 线程本地批量提交 - 减少原子操作竞争
2. **中优先级**: mimalloc 集成 - 可选特性，需要时启用
3. **低优先级**: CPU 亲和性 - 特定场景优化

## 结论

当前 xlog 已经使用了**预分配对象池**模式（环形缓冲区），这是最适合日志系统的方案：
- 零运行时分配
- 确定性延迟
- 缓存友好

**不建议**引入 mimalloc，原因：
1. 当前架构已避免运行时分配
2. 增加依赖复杂度
3. 收益有限

**建议**实现线程本地批量提交，这是最大的优化空间。

