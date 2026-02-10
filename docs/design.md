### 1. 核心架构图示

这个架构的核心思想是：**极速的前端（生产者） + 智能的后端（消费者）**。

------

### 2. 逐一拆解五大组件

#### 组件一：无锁 ring_buffer 队列 (Lock-free Ring Buffer)

这是心脏，直接决定了吞吐量（Throughput）和延迟（Latency）。

- **架构选择**：**MPSC (Multi-Producer Single-Consumer)** 模型。因为日志通常是多线程写，单线程后台落地。
- **实现关键**：
  - **原子操作**：使用 C11 `stdatomic.h` 的 `atomic_fetch_add` 来抢占写入索引（Write Index）。
  - **Cache Line Padding (伪共享优化)**：结构体中的 `head` 和 `tail` 指针必须分处于不同的缓存行（Cache Line，通常 64 字节），防止多核 CPU 频繁刷缓存导致性能下降。
  - **Power of 2 大小**：队列长度设为 2 的幂（如 65536），这样取模运算 `% size` 可以优化为位运算 `& (size - 1)`，CPU 处理速度快几倍。

#### 组件二：Quill 核心特性复刻 (The Quill Approach)

Quill 的快来源于“推迟一切耗时操作”。

- **延迟格式化 (Deferred Formatting)**：
  - **传统做法**：`sprintf(buf, "val=%d", 100)` -> 存入队列。 (慢，在业务线程做格式化)
  - **Quill/纯C做法**：存入结构体 `{ fmt_ptr, arg1, arg2 }` -> 存入队列。 (快，只拷贝内存，后台线程再做格式化)。
- **类型消除 (Type Erasure)**：
  - C 语言没有模板。需要定义一个通用的 `LogEvent` 结构体，或者使用紧凑的二进制协议（TLV：Tag-Length-Value），把 int、double、char* 紧凑地塞进 ring_buffer。
- **字符串处理 (String Handling)**：
  - **静态字符串**：只存指针。
  - **动态字符串**：必须深拷贝（Deep Copy）。为了性能，可以在 ring_buffer 的每个 Slot 预留一个固定的小 Buffer（如 32 字节），或者另开一个线性 buffer 存储长字符串。

#### 组件三：多 Sink 支持 (Multi-Sink Support)

日志不仅要写文件，可能还要上报监控、写控制台。

- **设计模式**：**V-Table (虚函数表) 的 C 语言模拟**。

- **实现**：定义一个 Sink 接口结构体。

  

  ```c
  typedef struct LogSink {
      void *context; // 文件句柄、Socket fd 等
      void (*write)(struct LogSink *self, const char *data, size_t len);
      void (*flush)(struct LogSink *self);
      void (*close)(struct LogSink *self);
  } LogSink;
  ```

- **路由策略**：Backend 线程格式化完一条日志后，遍历 `SinkList` 数组，调用每个 Sink 的 `write` 函数。

#### 组件四：丰富的格式定制 (Pattern Layout) ✅ 已实现

用户希望自定义 `[%d-%M-%Y %H:%m:%s] [INFO] [Thread-1] message`。

- **预编译模式 (Pre-compiled Pattern)** ✅：
  - 不要在每条日志打印时去解析格式字符串。
  - 在 `log_init` 时解析格式串，生成一个 **Token 链表**（`log_fmt_pattern`）。
  - Backend 线程执行时，只需遍历步骤数组快速填充数据。
  - 实现：`LOG_PATTERN_DEFAULT`, `LOG_PATTERN_SIMPLE`, `LOG_PATTERN_DEBUG` 等预定义模式
- **高性能时间戳** ✅：
  - 不要对每条日志调用 `localtime`（非常慢且有锁）。
  - **秒级缓存**：使用 `_Thread_local` 缓存最近一次格式化的秒级时间字符串（如 "2026-02-09 12:00:01"），只有微秒部分才实时计算。
  - **性能提升**：同一秒内的日志格式化速度提升 **~4x**

#### 组件五：Log Rotate 归档策略 (Rotation Policy)

文件不能无限增长，必须切割。

- **触发机制**：
  - **按大小 (Size-based)**：每次 `fwrite` 后检查 `current_bytes > max_bytes`。
  - **按时间 (Time-based)**：每天 0 点切割。
- **并发优势**：由于只有 Backend 一个线程在写文件，**完全不需要文件锁**。
- **实现逻辑**：
  1. 检测到阈值。
  2. `fclose(current_fp)`。
  3. `rename("app.log", "app.2023-10-27.log")`。
  4. `fopen("app.log", "w")`。
  5. 通知所有 File Sinks 更新句柄。

------

### 3. 数据结构设计 (Header 预览)

为了实现上述功能，核心数据结构设计如下：

```c
// 1. 紧凑的日志参数（支持 Quill 模式的延迟格式化）
typedef struct {
    union {
        int64_t i64;
        double  d64;
        char    *str; // 指向静态区或 deep_copy_buf
    } value;
    uint8_t type;     // 0=INT, 1=DOUBLE, 2=STATIC_STR, 3=DYNAMIC_STR
} LogArg;

// 2. 队列中的槽位 (Slot)
typedef struct {
    atomic_bool ready;       // 标志位
    const char *fmt_str;     // 原始格式串
    uint64_t timestamp_ns;   // 纳秒级时间戳
    int32_t thread_id;       
    int arg_count;
    LogArg args[8];          // 最多支持8个参数
    char deep_copy_buf[64];  // 动态字符串的小型缓冲区
} LogEvent;

// 3. 全局上下文
typedef struct {
    LogEvent *ring_buffer;
    size_t mask;
    // ... 原子索引 ...
    LogSink **sinks;         // Sink 指针数组
    int sink_count;
    // ... 归档策略配置 ...
} Logger;
```

------

### 4. 性能优化 Checklist (针对纯 C)

为了达到 Quill 的级别，除了架构，还需注意细节：

1. **CPU 亲和性 (Affinity)**：将 Backend 线程绑定到特定的 CPU 核上，减少上下文切换。
2. **批处理写入 (Batch Write)**：Backend 线程从队列取出日志后，不要一条一条 `fwrite`，而是先写入一个 4KB 的 `char buffer`，满了再由 OS 刷盘。
3. **分支预测优化**：使用 `likely()` / `unlikely()` 宏标记热路径（Hot Path）中的判断条件（如队列是否满）。
4. **避免 False Sharing**：确保 `producer_idx` 和 `consumer_idx` 在内存中距离至少 64 字节（Cache Line 对齐）。

------

## Ring Buffer Overflow Policies
- `RB_POLICY_DROP`: drop newest when full, increment `dropped`.
- `RB_POLICY_DROP_OLDEST`: evict oldest slot (advances read index) and keep accepting new writes.
- `RB_POLICY_SPIN`: busy-wait until space is available.
- `RB_POLICY_BLOCK`: yield while waiting for space; no drops by design.

`LogEntry` is padded/aligned to cache-line size and the buffer is allocated with `posix_memalign` to avoid false sharing.

### 下一步行动

设计已经非常清晰。这是一个具备商业级潜力的 C 语言日志库蓝图。

**您希望我先为您实现哪一部分的核心代码？**

1. **无锁 ring_buffer + 生产者入队逻辑**（这是最难也是最核心的部分）。
2. **多 Sink + 格式化引擎**（这是业务逻辑最复杂的部分）。
3. **整合版**：一个包含上述所有 5 点的基础可运行框架（代码量会稍大，约 500 行）。

---

## 完成总结 (2026-02-09 更新)

### 已实现的组件

| 组件 | 文件 | 功能 |
|------|------|------|
| ✅ 无锁 Ring Buffer | `ringbuf.h/c` | MPSC 队列，支持 DROP/SPIN/BLOCK 等策略 |
| ✅ 延迟格式化 Log Record | `log_record.h/c` | TLV 协议，类型擦除，高性能格式化 |
| ✅ Sink 抽象接口 | `sink.h/c` | V-Table 模式，Sink Manager |
| ✅ Console Sink | `console_sink.h/c` | stdout/stderr，TTY 检测 |
| ✅ File Sink | `file_sink.h/c` | 文件输出，缓冲写入 |
| ✅ xlog 主 API | `xlog.h/c` | Backend 线程，同步/异步模式 |
| ✅ 日志宏 | `xlog.h` | XLOG_TRACE/DEBUG/INFO/WARN/ERROR/FATAL |
| ✅ 跨平台兼容层 | `platform.h/c` | Linux/macOS/Windows 统一 API |
| ✅ 高级 Log Rotation | `rotate.h/c` | 按大小/日期归档，目录容量限制 |
| ✅ SIMD 加速 | `simd.h/c` | SSE2/SSE4.2/AVX2/NEON 跨平台优化 |
| ✅ 批量写入器 | `batch_writer.h/c` | 缓冲 I/O，Direct I/O，mmap 模式 |
| ✅ ANSI 颜色输出 | `color.h/c` | 跨平台颜色支持，多种配色方案 |
| ✅ Syslog Sink | `syslog_sink.h/c` | POSIX syslog 集成，支持多种 facility |
| ✅ 配置 Builder | `xlog_builder.h/c` | 链式配置 API，预设配置，快速启动 |
| ✅ 统一 API 入口 | `include/xlog.h` | 用户唯一需要的头文件，无依赖 |

### 文件结构

```
xlog/
├── include/
│   └── xlog.h          # 公共 API（用户只需要这一个头文件）
├── src/
│   ├── xlog_core.h     # 内部核心实现
│   ├── xlog_builder.h  # Builder 实现
│   ├── ...             # 其他内部模块
├── lib/
│   └── libxlog.a       # 静态库
└── examples/
    ├── example_simple.c
    └── example_full.c
```

### 用户使用方式

```c
// 只需要一个头文件
#include <xlog.h>

// 编译
// gcc -I/path/to/xlog/include myapp.c -L/path/to/xlog/lib -lxlog -lpthread
```

### API 概览

#### 日志宏 (两种风格)

```c
// 推荐风格 (XLOG_*)
XLOG_TRACE(fmt, ...)
XLOG_DEBUG(fmt, ...)
XLOG_INFO(fmt, ...)
XLOG_WARN(fmt, ...)
XLOG_ERROR(fmt, ...)
XLOG_FATAL(fmt, ...)

// 兼容风格 (LOG_*) - 与旧代码兼容
LOG_TRACE(fmt, ...)
LOG_DEBUG(fmt, ...)
LOG_INFO(fmt, ...)
LOG_WARN(fmt, ...)
LOG_ERROR(fmt, ...)
LOG_FATAL(fmt, ...)

// 条件日志
XLOG_DEBUG_IF(condition, fmt, ...)
LOG_DEBUG_IF(condition, fmt, ...)  // 兼容

// 带上下文
XLOG_MODULE(level, "module_name", fmt, ...)
XLOG_TAG(level, "tag_name", fmt, ...)
```

#### 快速初始化

```c
xlog_init_console(LOG_LEVEL_DEBUG);              // 仅控制台
xlog_init_file("/logs", "app", LOG_LEVEL_INFO);  // 控制台+文件
xlog_init_full("/logs", "app", LOG_LEVEL_DEBUG); // 全部
xlog_init_daemon("/logs", "app", LOG_LEVEL_INFO);// 守护进程
```

#### Builder API (链式配置)

```c
xlog_builder *cfg = xlog_builder_new();
xlog_builder_set_name(cfg, "myapp");
xlog_builder_enable_file(cfg, true);
xlog_builder_file_max_size(cfg, 50 * XLOG_1MB);
xlog_builder_apply(cfg);
xlog_builder_free(cfg);
```

#### 核心 API

```c
xlog_init() / xlog_shutdown()    // 生命周期
xlog_set_level() / xlog_get_level()  // 级别控制
xlog_add_sink() / xlog_remove_sink() // Sink 管理
xlog_flush()                      // 刷新缓冲区
```

### 跨平台支持 (platform.h)

- **平台检测**: `XLOG_PLATFORM_LINUX`, `XLOG_PLATFORM_MACOS`, `XLOG_PLATFORM_WINDOWS`
- **编译器优化**: `XLOG_LIKELY/UNLIKELY`, `XLOG_CPU_PAUSE`, `XLOG_MEMORY_BARRIER`
- **线程 API**: `xlog_thread_t`, `xlog_mutex_t`, `xlog_cond_t`
- **文件系统**: `xlog_file_size`, `xlog_mkdir_p`, `xlog_rename`, `xlog_list_files`
- **高精度时间**: `xlog_get_timestamp_ns`, `xlog_get_localtime`

### Log Rotation 策略 (rotate.h)

**归档命名规则**:
```
pel.log              # 当前活动文件
pel-20260209.log     # 当天第一个归档
pel-20260209-01.log  # 当天第二个归档（超过单文件大小限制）
pel-20260209-02.log  # 当天第三个归档
pel-20260208.log     # 前一天的归档
```

**配置选项**:
- `max_file_size`: 单文件大小限制（默认 50MB）
- `max_dir_size`: 目录总容量限制（默认 500MB）
- `max_files`: 最大归档文件数（默认 100）
- `rotate_on_start`: 启动时检查并归档（边缘情况处理）

**特性**:
- 按大小自动轮转
- 按日期自动轮转
- 序号递增 (01, 02, 03...)
- 目录容量限制（自动删除最旧文件）
- 启动时验证并处理遗留文件

### 测试覆盖

- `test_ringbuf.c` - Ring Buffer 基础测试
- `test_ringbuf_concurrency.c` - 并发测试
- `test_ringbuf_policy.c` - Overflow 策略测试
- `test_log_record.c` - 格式化协议测试
- `test_xlog.c` - 主 API 集成测试
- `test_rotate.c` - Log Rotation 逻辑测试
- `test_simd.c` - SIMD 加速和批量写入测试
- `test_color.c` - ANSI 颜色输出测试
- `test_syslog_sink.c` - Syslog Sink 测试
- `test_builder.c` - 配置 Builder API 测试
- `test_legacy_macros.c` - LOG_* 宏兼容性测试
- `bench_ringbuf.c` - 性能基准测试

### 下一步可选工作

1. ~~将 `rotate` 模块与 `file_sink` 集成~~ ✅ 已完成
2. 添加网络 Sink（UDP/TCP）
3. ~~添加 syslog Sink~~ ✅ 已完成
4. ~~ANSI 颜色输出支持~~ ✅ 已完成
5. ~~性能优化（SIMD 格式化，批量写入）~~ ✅ 已完成
6. 完整文档和示例程序

---

## 性能优化模块 (2026-02-09)

### SIMD 跨平台加速 (simd.h/c)

**支持的架构**:
| 架构 | 指令集 | 自动检测 |
|------|--------|----------|
| x86_64 | SSE2, SSE4.2, AVX2 | ✅ CPUID 运行时检测 |
| ARM64 | NEON | ✅ 编译时启用 |
| x86 (32-bit) | SSE2, SSE4.2 | ✅ CPUID 运行时检测 |
| ARM32 | NEON (可选) | ✅ 编译时检测 |

**优化的操作**:

1. **整数转字符串** (`xlog_simd_u64toa`, `xlog_simd_i64toa`)
   - 使用预计算的 2 位数查找表 (00-99)
   - 比 `sprintf` 快 **2x**

2. **日期时间格式化** (`xlog_simd_format_datetime`, `xlog_simd_format_usec`)
   - 直接使用查找表写入，无需解析格式字符串
   - 比 `sprintf("%04d-%02d-%02d ...")` 快 **15x+**

3. **内存操作** (`xlog_simd_memcpy`, `xlog_simd_memset`, `xlog_simd_strlen`)
   - **Linux/macOS**: 直接委托给 glibc/libSystem（已有 AVX2/AVX-512 汇编优化）
   - **Windows**: 使用自定义 SSE2/AVX2 实现（MSVC CRT 优化较弱）
   - **设计原则**: 不重复造轮子，利用已有的高度优化实现

4. **格式字符串解析** (`xlog_simd_find_percent`, `xlog_simd_count_specifiers`)
   - 使用 SIMD 快速查找 `%` 字符

**使用示例**:
```c
#include "simd.h"

// 运行时检测 CPU 特性
const xlog_cpu_features *features = xlog_get_cpu_features();
printf("AVX2: %s\n", features->avx2 ? "yes" : "no");

// 快速整数转字符串
char buf[32];
int len = xlog_simd_u64toa(123456789, buf);  // 比 sprintf 快 2x

// 快速日期时间格式化
xlog_simd_format_datetime(2026, 2, 9, 14, 30, 45, buf);  // 比 sprintf 快 15x
```

### 批量写入器 (batch_writer.h/c)

**设计目标**: 减少系统调用次数，提高 I/O 吞吐量

**配置选项**:
```c
typedef struct batch_writer_config {
    size_t   buffer_size;        // 缓冲区大小 (默认 4KB)
    float    flush_threshold;    // 刷新阈值 (默认 80%)
    uint32_t max_pending;        // 最大待处理条数 (默认 64)
    uint32_t flush_timeout_ms;   // 超时刷新 (默认 100ms)
    bool     use_direct_io;      // Linux O_DIRECT 模式
    bool     use_write_combine;  // 合并小写入
} batch_writer_config;
```

**I/O 模式**:
| 模式 | 平台 | 特点 |
|------|------|------|
| STDIO | 全平台 | 标准 FILE* 操作 |
| Direct I/O | Linux | O_DIRECT，绕过页缓存 |
| Memory-mapped | Linux/macOS/Windows | mmap，适合顺序写入 |

**API 接口**:
```c
// 创建批量写入器
batch_writer *writer = batch_writer_create(fp, &config);

// 写入数据（自动缓冲）
batch_writer_write(writer, data, len);
batch_writer_printf(writer, "Value: %d\n", 42);

// 零拷贝模式
char *ptr = batch_writer_reserve(writer, 100);
int written = sprintf(ptr, "Direct: %d", 42);
batch_writer_commit(writer, written);

// 手动刷新
batch_writer_flush(writer);

// 获取统计
batch_writer_stats stats;
batch_writer_get_stats(writer, &stats);
```

**性能对比** (100,000 条日志，每条 ~100 字节):
```
┌─────────────────────────────────┬────────────┬────────────┬─────────┐
│ Method                          │ Time (ms)  │ Throughput │ Syscalls│
├─────────────────────────────────┼────────────┼────────────┼─────────┤
│ Batch Writer (32KB buf)         │     3.76   │   2663 MB/s │     339 │
│ Direct write() syscall          │    27.57   │    363 MB/s │  100000 │
│ fwrite (with stdio buffer)      │     4.16   │   2404 MB/s │   ~few  │
│ fwrite (no buffer, _IONBF)      │    24.48   │    409 MB/s │  100000 │
└─────────────────────────────────┴────────────┴────────────┴─────────┘
```

**核心价值**：
- vs 直接 syscall: **7x 加速**
- vs 无缓冲写入: **6x 加速**
- 精确控制刷盘时机（超时、阈值、条数）
- 支持 Direct I/O 和 mmap 模式
- 零拷贝 `reserve/commit` API

**使用场景**：
- ✅ 需要精确控制刷盘时机
- ✅ 使用 Direct I/O 或 mmap
- ✅ 需要写入统计信息
- ❌ 简单场景用 `fwrite` 即可（stdio 已有 ~4-8KB 缓冲）

### CMake 配置

```cmake
# 启用/禁用 SIMD 优化
option(ENABLE_SIMD "Enable SIMD optimizations" ON)

# 自动检测并添加编译标志
# Linux/macOS: -msse4.2 -mavx2
# Windows MSVC: /arch:AVX2
# ARM64: NEON 默认启用
```

---

## ANSI 颜色输出模块 (color.h/c)

### 跨平台支持

| 平台 | 支持方式 |
|------|----------|
| Linux | 原生 ANSI 支持 |
| macOS | 原生 ANSI 支持 |
| Windows 10+ | 通过 `SetConsoleMode` 启用 VT 处理 |
| Windows 旧版 | 自动降级为无颜色 |

### 预定义配色方案

| 方案 | 说明 |
|------|------|
| `XLOG_SCHEME_DEFAULT` | 默认配色（绿色 INFO，红色 ERROR 等）|
| `XLOG_SCHEME_VIVID` | 高对比度亮色 |
| `XLOG_SCHEME_PASTEL` | 柔和的淡色 |
| `XLOG_SCHEME_MONOCHROME` | 仅使用样式（粗体、下划线）|

### API 使用

```c
#include "color.h"

// 初始化（Windows 需要启用 ANSI）
xlog_color_init();

// 设置颜色模式
xlog_color_set_mode(XLOG_COLOR_AUTO);    // 自动检测 TTY
xlog_color_set_mode(XLOG_COLOR_ALWAYS);  // 强制颜色
xlog_color_set_mode(XLOG_COLOR_NEVER);   // 禁用颜色

// 切换配色方案
xlog_color_set_custom(xlog_color_get_scheme(XLOG_SCHEME_VIVID));

// 格式化带颜色的日志级别
char buf[64];
xlog_color_format_level(buf, sizeof(buf), LOG_LEVEL_ERROR);
// 结果: "\033[1m\033[31mERROR\033[0m"

// 剥离 ANSI 颜色码
char stripped[64];
xlog_color_strip(stripped, sizeof(stripped), colored_text);

// 计算显示宽度（排除不可见的 ANSI 码）
size_t width = xlog_color_display_width(colored_text);
```

### 日志级别颜色（默认方案）

| 级别 | 颜色 |
|------|------|
| TRACE | 暗灰 (Dim White) |
| DEBUG | 青色 (Cyan) |
| INFO | 绿色 (Green) |
| WARN | 粗体黄色 (Bold Yellow) |
| ERROR | 粗体红色 (Bold Red) |
| FATAL | 白字红底 (White on Red) |

---

## Syslog Sink 模块 (syslog_sink.h/c)

### 平台支持

| 平台 | 支持 |
|------|------|
| Linux | ✅ 原生 syslog |
| macOS | ✅ 原生 syslog |
| BSD | ✅ 原生 syslog |
| Windows | ❌ 不支持（可用 Windows Event Log 替代）|

### Syslog Facility

| Facility | 说明 | 使用场景 |
|----------|------|----------|
| `SYSLOG_FACILITY_USER` | 用户级消息（默认）| 普通应用程序 |
| `SYSLOG_FACILITY_DAEMON` | 系统守护进程 | 后台服务 |
| `SYSLOG_FACILITY_AUTH` | 认证相关 | 安全模块 |
| `SYSLOG_FACILITY_LOCAL0-7` | 本地使用 | 自定义分类 |

### 日志级别映射

| xlog 级别 | syslog 优先级 |
|-----------|---------------|
| TRACE | LOG_DEBUG (7) |
| DEBUG | LOG_DEBUG (7) |
| INFO | LOG_INFO (6) |
| WARNING | LOG_WARNING (4) |
| ERROR | LOG_ERR (3) |
| FATAL | LOG_CRIT (2) |

### API 使用

```c
#include "syslog_sink.h"

// 创建默认 syslog sink（USER facility，包含 PID）
sink_t *syslog = syslog_sink_create_default("myapp", LOG_LEVEL_INFO);

// 创建守护进程 syslog sink（DAEMON facility）
sink_t *daemon_log = syslog_sink_create_daemon("mydaemon", LOG_LEVEL_DEBUG);

// 创建自定义 facility
sink_t *custom = syslog_sink_create_with_facility("myapp", 
                                                   SYSLOG_FACILITY_LOCAL0, 
                                                   LOG_LEVEL_INFO);

// 完整配置
syslog_sink_config config = {
    .ident = "myapp",
    .facility = SYSLOG_FACILITY_LOCAL1,
    .include_pid = true,
    .log_perror = false  // 同时输出到 stderr
};
sink_t *sink = syslog_sink_create(&config, LOG_LEVEL_DEBUG);

// 添加到 xlog
xlog_add_sink(syslog);

// 使用
XLOG_INFO("Application started");
XLOG_ERROR("Connection failed: %s", strerror(errno));

// 查看日志
// journalctl -t myapp --since '5 minutes ago'
// grep myapp /var/log/syslog | tail -10
```

### 与 systemd 集成

当程序作为 systemd 服务运行时，syslog 消息会自动被 journald 捕获：

```ini
# /etc/systemd/system/myapp.service
[Unit]
Description=My Application

[Service]
ExecStart=/usr/local/bin/myapp
StandardOutput=journal
StandardError=journal
SyslogIdentifier=myapp

[Install]
WantedBy=multi-user.target
```

查看日志：
```bash
journalctl -u myapp -f          # 实时跟踪
journalctl -u myapp --since today  # 今天的日志
journalctl -t myapp -p err      # 只看错误级别
```

---

## 配置 Builder API (xlog_builder.h/c)

### 设计理念

采用 **Builder 模式**，提供链式配置风格，简化 xlog 的初始化流程。

### 快速启动 API（一行配置）

```c
#include "xlog_builder.h"

// 方式 1: 仅控制台输出（开发调试）
xlog_init_console(LOG_LEVEL_DEBUG);

// 方式 2: 控制台 + 文件输出
xlog_init_file("/var/log/myapp", "myapp", LOG_LEVEL_INFO);

// 方式 3: 完整输出（控制台 + 文件 + syslog）
xlog_init_full("/var/log/myapp", "myapp", LOG_LEVEL_DEBUG);

// 方式 4: 守护进程模式（文件 + syslog，无控制台）
xlog_init_daemon("/var/log/myapp", "mydaemon", LOG_LEVEL_INFO);
```

### 链式配置 API（完整控制）

```c
#include "xlog_builder.h"

// 创建配置构建器
xlog_builder *cfg = xlog_builder_new();

// 链式配置 - 全局设置
xlog_builder_set_name(cfg, "my_application");
xlog_builder_set_level(cfg, LOG_LEVEL_DEBUG);
xlog_builder_set_mode(cfg, XLOG_MODE_ASYNC);
xlog_builder_set_buffer_size(cfg, 16384);

// 链式配置 - 控制台 Sink
xlog_builder_enable_console(cfg, true);
xlog_builder_console_level(cfg, LOG_LEVEL_DEBUG);
xlog_builder_console_target(cfg, XLOG_CONSOLE_STDOUT);
xlog_builder_console_color(cfg, XLOG_COLOR_ALWAYS);
xlog_builder_console_flush(cfg, true);

// 链式配置 - 文件 Sink
xlog_builder_enable_file(cfg, true);
xlog_builder_file_level(cfg, LOG_LEVEL_INFO);
xlog_builder_file_directory(cfg, "/var/log/myapp");
xlog_builder_file_name(cfg, "app");
xlog_builder_file_extension(cfg, ".log");
xlog_builder_file_max_size(cfg, 50 * XLOG_1MB);      // 50MB per file
xlog_builder_file_max_dir_size(cfg, 500 * XLOG_1MB); // 500MB total
xlog_builder_file_max_files(cfg, 100);
xlog_builder_file_rotate_on_start(cfg, true);

// 链式配置 - Syslog Sink (Linux/macOS)
xlog_builder_enable_syslog(cfg, true);
xlog_builder_syslog_level(cfg, LOG_LEVEL_ERROR);
xlog_builder_syslog_ident(cfg, "myapp");
xlog_builder_syslog_facility(cfg, XLOG_SYSLOG_DAEMON);
xlog_builder_syslog_pid(cfg, true);

// 应用配置
xlog_builder_apply(cfg);

// 打印配置摘要
char dump[4096];
xlog_builder_dump(cfg, dump, sizeof(dump));
printf("%s\n", dump);

// 使用日志
XLOG_INFO("Application started");
XLOG_ERROR("Error occurred: %d", errno);

// 清理
xlog_builder_free(cfg);
xlog_shutdown();
```

### 预设配置

```c
// 开发环境：彩色控制台，DEBUG 级别，显示详细信息
xlog_builder *dev = xlog_preset_development();
xlog_builder_apply(dev);

// 生产环境：仅文件，INFO 级别，标准轮转设置
xlog_builder *prod = xlog_preset_production("/var/log", "myapp");
xlog_builder_apply(prod);

// 测试环境：TRACE 级别，小文件便于快速轮转
xlog_builder *test = xlog_preset_testing("/tmp/test_logs");
xlog_builder_apply(test);
```

### 配置选项一览

| 类别 | 选项 | 默认值 | 说明 |
|------|------|--------|------|
| **全局** | `app_name` | "xlog" | 应用名称 |
| | `global_level` | DEBUG | 全局最低日志级别 |
| | `mode` | ASYNC | ASYNC=异步, SYNC=同步 |
| | `buffer_size` | 8192 | Ring Buffer 槽位数 |
| **控制台** | `enabled` | true | 是否启用 |
| | `level` | DEBUG | 最低日志级别 |
| | `target` | STDOUT | STDOUT 或 STDERR |
| | `color_mode` | AUTO | AUTO/ALWAYS/NEVER |
| | `flush_on_write` | true | 每次写入后刷新 |
| **文件** | `enabled` | false | 是否启用 |
| | `level` | DEBUG | 最低日志级别 |
| | `directory` | "./logs" | 日志目录 |
| | `base_name` | "app" | 基础文件名 |
| | `extension` | ".log" | 文件扩展名 |
| | `max_file_size` | 50MB | 单文件大小限制 |
| | `max_dir_size` | 500MB | 目录总大小限制 |
| | `max_files` | 100 | 最大归档文件数 |
| | `rotate_on_start` | true | 启动时检查轮转 |
| **Syslog** | `enabled` | false | 是否启用 |
| | `level` | INFO | 最低日志级别 |
| | `ident` | app_name | 程序标识符 |
| | `facility` | USER | syslog facility |
| | `include_pid` | true | 是否包含 PID |

### 大小常量

```c
#define XLOG_1KB    (1024ULL)
#define XLOG_1MB    (1024ULL * 1024ULL)
#define XLOG_1GB    (1024ULL * 1024ULL * 1024ULL)

// 使用示例
xlog_builder_file_max_size(cfg, 100 * XLOG_1MB);  // 100MB
xlog_builder_file_max_dir_size(cfg, 2 * XLOG_1GB); // 2GB
```
