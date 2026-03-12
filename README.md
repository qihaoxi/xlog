# xlog: 高性能异步无锁 C 日志库

`xlog` 是一个受 **Quill** 启发、为极致性能而生的纯 C 异步日志库。它通过无锁环形缓冲区（Lock-free Ring Buffer）实现了生产者与消费者的解耦，确保在记录日志时对业务线程的干扰降至最低（纳秒级延迟）。

## ✨ 核心特性

- **⚡ 极致性能**：基于 C11 原子操作实现的无锁单/多生产者环形缓冲区，有效避免线程竞争。
- **🧵 异步落盘**：独立的后端线程负责格式化与 I/O 操作，业务线程“即写即走”。
- **📦 单头文件设计**：仅需一个 `xlog.h` 即可集成，无繁琐的依赖管理。
- **🌈 多端输出 (Sinks)**：支持控制台（带颜色）、文件流、以及系统级 `syslog`。
- **🔄 日志管理**：内置文件滚动（Rotation）与异步压缩（Gzip）功能。
- **🛠️ 结构化输出**：支持自定义 Formatter，可输出为 **文本**、**JSON** 或 **Raw** 原始格式。
- **🛡️ 鲁棒性**：处理了缓存行对齐（Cache Line Alignment）以避免伪共享问题。

## 🚀 快速上手

### 1. 引入头文件

只需在项目中包含主头文件：

```c
#include <xlog.h>
```

### 2. 初始化 (Initialization)

您可以选择最简单的单行初始化，或者使用构建器进行复杂配置。

**方式一：简单初始化 (Console Only)**

```c
int main(void) {
    // 初始化控制台日志，仅显示 DEBUG 及以上级别
    xlog_init_console(XLOG_LEVEL_DEBUG);
    
    LOG_INFO("Hello, xlog!");
    LOG_WARN("This is a warning");
    
    xlog_shutdown();
    return 0;
}
```

**方式二：文件日志 (Console + File)**

```c
int main(void) {
    // 自动在 logs 目录下创建 myapp.log
    xlog_init_file("logs", "myapp", XLOG_LEVEL_INFO);
    
    LOG_INFO("Application started");
    
    xlog_shutdown();
    return 0;
}
```

**方式三：高级配置 (Builder Pattern)**

```c
int main(void) {
    xlog_builder *cfg = xlog_builder_new();

    // 1. 全局设置
    xlog_builder_set_name(cfg, "MyService");
    xlog_builder_set_level(cfg, XLOG_LEVEL_DEBUG);

    // 2. 启用控制台彩色输出
    xlog_builder_enable_console(cfg, true);
    xlog_builder_console_color(cfg, XLOG_COLOR_ALWAYS);

    // 3. 配置文件的轮转与压缩
    xlog_builder_enable_file(cfg, true);
    xlog_builder_file_directory(cfg, "/var/log/myapp");
    xlog_builder_file_name(cfg, "service_log");
    xlog_builder_file_max_size(cfg, 100 * XLOG_1MB); // 单个文件最大 100MB
    xlog_builder_file_max_files(cfg, 10);            // 最多保留 10 个文件
    xlog_builder_file_compress(cfg, true);           // 启用旧日志压缩

    // 4. 应用配置
    xlog_builder_apply(cfg);
    xlog_builder_free(cfg);

    LOG_INFO("System initialized with custom config");
    
    xlog_shutdown();
    return 0;
}
```

## 🛠️ 编译与集成 (Build & Integration)

### 编译库

```bash
mkdir build && cd build
cmake ..
make
```

###在项目中使用

在您的 `CMakeLists.txt` 中：

```cmake
add_subdirectory(path/to/xlog)
target_link_libraries(your_app PRIVATE xlog) // 或者 xlog_shared
```

## 📜 API 参考

### 日志宏

推荐使用以下标准宏进行日志记录，它们支持类似 `printf` 的格式化字符串：

*   `LOG_TRACE(fmt, ...)`
*   `LOG_DEBUG(fmt, ...)`
*   `LOG_INFO(fmt, ...)`
*   `LOG_WARN(fmt, ...)`
*   `LOG_ERROR(fmt, ...)`
*   `LOG_FATAL(fmt, ...)`

### 配置选项

*   **日志级别**: `XLOG_LEVEL_TRACE` ~ `XLOG_LEVEL_OFF`
*   **运行模式**: `XLOG_MODE_ASYNC` (默认), `XLOG_MODE_SYNC`

## 📁 目录结构

*   `include/`: 公共头文件
*   `src/`: 源代码实现 (Core, Sinks, Compression, etc.)
*   `examples/`: 使用示例
*   `tests/`: 单元测试

---

## 📄 License

MIT License

