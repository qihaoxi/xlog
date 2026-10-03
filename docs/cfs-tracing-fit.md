# 选型评估：xlog 承载 CFS-DESIGN §19 全链路追踪

- 状态: 评估意见（待 CFS 侧裁决）
- 日期: 2026-08-26（v2：收敛为最小边界版本——砍传播 API 与 span 生命周期提案，16B 改不透明通道）
- 相关: `CFS-DESIGN.md` §19（编号错乱为 18.x）、`STRACE-DESIGN.md`（strace 模式 / 离线归并工具 strace_merge）

## 1. 背景

CFS 需要为 syscall/系统调用路径提供全链路追踪（distributed tracing），目标是跨进程
（`proot` 宿主进程内多个子进程）串起一次调用的完整链路，并以 JSON Lines 落地，
支持离线归并（strace_merge）。

评估对象：现有 `xlog` 日志库能否直接承载该方案，还是需要另起/扩展。

## 2. 边界定调：谁负责什么

xlog 的设计源自 Quill：前端编码、无锁队列、后端延迟格式化——它是**低延迟传输管道**，
不是 tracing 系统。Quill 自身也不做 span 生命周期与上下文传播；trace 语义历来是
消费方（相当于 OpenTelemetry 的位置）的事。STRACE-DESIGN 的实际设计恰好也是这么分的：

| # | 需求（CFS-DESIGN §19） | 归属 | 依据（STRACE-DESIGN） |
|---|------|------|------|
| R1 | 16B 定长 Trace ID | 生成在 CFS 薄壳；xlog 只搬运 | 薄壳生成写入 env `CFS_STRACE_ID`（§2） |
| R2 | 无锁单生产者队列 | xlog（已具备） | §18 队列即 xlog 的 ring buffer |
| R3 | 异步后台线程 | xlog（已具备） | 后台线程落盘，tracer 禁同步 write |
| R4 | JSON Lines 带 Trace ID | xlog（已具备，载运宽度见 §4.1） | 宿主 VM/donkey 行以 JSON Lines 写标准日志 |
| R5 | 四段埋点 | 埋点在 CFS；字段搬运在 xlog | enter/exit 记录 + 32B 帧头三件套 |
| R6 | 跨进程传播 | **CFS，非 xlog** | env + INHERIT_PARENT（§3）；宿主 VM 走 `interp_frame_t`/回调（§4）；donkey 不传播、按 trace_id 捞取（§6） |

一句话边界：**CFS 是 tracing 系统（管 ID 生成、传播、离线建树），xlog 是低延迟
JSON Lines 管道（管搬运和格式化）。** 按这个边界，xlog 只欠「暴露 API + 少量字段
容量」，不欠一次设计变更。

## 3. xlog 现状核对（对源码确认，非猜测）

| 组件 | 位置 | 说明 |
|------|------|------|
| MPSC 无锁环形缓冲 | `src/ringbuf.h` | `rb_reserve` / `rb_commit`，多生产者单消费者 |
| 异步后台线程 | `src/xlog.c` | 后端消费线程落地 |
| JSON Lines Formatter | `src/formatter.c:489,621,627` | 输出 `"trace_id":"%016PRIx64"`、`"span_id":"%016PRIx64"` |
| trace API | `src/log_record.h` | `log_context{trace_id, span_id}`（均 `uint64_t`，行 174-175）、`log_record_set_trace()`（行 955）、`log_record_add_trace_id_field`（行 1069） |
| TLV 自定义字段 | `src/log_record.h:44,971+` | `log_record_add_field_str/int/float/binary`，上限 `LOG_MAX_CUSTOM_FIELDS = 2` |
| 公开头 | `include/xlog.h` | 精简版，**未暴露任何 trace API**（grep 无命中）；完整在 `single_include/xlog.h` |

### 3.1 已具备（可满足）

- **无锁队列 + 异步线程**（R2/R3）：MPSC ring buffer 完全符合「单生产者异步落地」诉求，CFS 每个埋点线程即有独立队列。
- **JSON Lines（R4）**：formatter 原生输出 JSON，且已支持 `trace_id`/`span_id` 顶层字段。
- **Trace ID / Span ID 写入**（R1 部分）：`log_record_set_trace(uint64, uint64)` 可直接写入关联 ID。

### 3.2 缺口（确认为项目内不存在）

grep 全源码 `src/`、`include/` 未命中以下概念：

- **16B trace 载运通道**：现为 64-bit `trace_id` 单字段，放不下 16B。缺的是**通道**，
  不是把 `{ts_ns, session, entry_hash}` 的结构语义放进 xlog（见 §4.2）。
- **`parent_span_id`**：无此字段，JSON 输出也无。→ 影响 R5 四段埋点的层级关系还原。
- **`dur` 数值字段**：无。但只需一个普通数值字段——埋点处算好传入即可，
  **不需要**库内 span 生命周期接口（见 §4.2）。
- **trace API 未进公开头**：`include/xlog.h` 仅暴露 `xlog_log`/宏，C 消费者
  （`vlibvfd/vlibvfs`、`proot`）主头拿不到。
- **自定义字段容量**：`LOG_MAX_CUSTOM_FIELDS = 2`。一条 strace 行要带
  `parent_span_id` + `dur_ns`（可能还有 syscall 号）即超限。

> **v1 修正**：前版把「跨进程传播 API」列为 xlog 最大缺口，系归因错误——STRACE-DESIGN
> 已在 CFS 层完成全部传播设计（薄壳写 env、INHERIT_PARENT 传播、宿主 VM 回调注入、
> donkey 按 trace_id 捞取），xlog 的职责仅是接受调用方每记录给出的 ID
> （`log_record_set_trace()` 已存在）。

## 4. 结论：扩展 xlog 承载（**已于 2026-08-26 落地为通用能力**）

xlog 已具备无锁队列、异步线程、JSON Lines、trace/span 基础字段，性能模型与需求一致。
另起新库代价高（重复 ring/格式化/异步），不划算。落地实现全部是**加法**，且以通用
日志库能力的形式交付（与 CFS 无耦合）：128-bit trace 采用 W3C Trace Context /
OpenTelemetry 布局，parent span 是 OTel 标准模型，字段扩容是通用 kv 容量。

### 4.1 落地清单（全部完成）

1. **线程本底（ambient）trace 上下文 API**——`xlog_trace_set/get/clear`
   （`include/xlog.h` 公开头 + `src/xlog_core.h` 内部原型）。
   MDC 风格：设一次，本线程后续每条日志自动盖章；`xlog_log_ctx()` 显式上下文
   优先于本底值。未设置时热路径只付一次 TLS 标志检查。可在 `xlog_init()` 前使用。
2. **128-bit 不透明 trace 通道**——`log_context.trace_id_hi` + `LOG_CTX_HAS_TRACE_ID_HI`
   flag + `log_record_set_trace_full(hi, lo, span, parent)`。对 xlog 是 16B 不透明
   数据；JSON 输出 hi 在前拼为 32-hex `"trace_id"`，仅低 64 位时保持 16-hex（向后兼容）。
   旧的 `log_record_set_trace()` 语义不变（并清除 hi/parent 位）。
3. **`parent_span_id` 固定字段**——`log_context` + flag + JSON 顶层键
   `"parent_span_id"`，与 `span_id` 对称，strace_merge 顶层直读。0 = 根 span 不输出。
4. **`dur_ns` 等任意数值走 TLV 自定义字段**——埋点处算好传入，无库内 span 生命周期。
5. **`LOG_MAX_CUSTOM_FIELDS` 2 → 4**——通用容量参数。

**实测（gcc x86-64 探针）**：`log_context` 48→64B；`log_record` 256→**320B（5 缓存行）**。
对齐自愈（`LOG_RECORD_PAD_SIZE` 重算 + `_Static_assert`），`ready` 仍在行 0，
伪共享格局不变；`rb_reserve` 只 reset 头部、未用字段区不写、无整条 memcpy，
故 ring 内存 +25%（65536 槽 16→20MB）与后端读流量是仅有的代价。

**验证矩阵（全部通过）**：gcc / clang 全量 14 测试；`STRICT_CHECKS=ON`
（-Wall -Wextra -Werror -Wmissing-prototypes）；`-Wpedantic` 消费者
（公开头 strict -std=c11 × gcc/clang；单头 gnu11 + 项目旗标 × gcc/clang）；
ASan 全量；TSan 全量。绑核交替 5 轮 bench：前端 async 热路径 HEAD vs 新代码
不可区分（min ≈ 81ns/call，中位数 93 vs 92），**零可测回归**。
新增测试：`test_log_record`（flag 语义/reset/容量）、`test_formatter`
（JSON 16/32-hex/parent）、`test_xlog` Test 7（TLS 端到端 JSON 断言）。

> 附带发现并已修复的存量问题：TSan 曾在 `rb_push`（ringbuf.c）报 data race——
> 整结构 memcpy 非原子覆写 `ready`，与消费者 acquire 载入竞争，且源记录已 commit
> 时可能发布撕裂记录。修复：memcpy 自 `level` 起（跳过首成员 atomic `ready`），
> 发布交由 `rb_commit` 的 release 序；HEAD 可复现旧问题，修复后 TSan 全量通过。
> `CACHE_LINE_SIZE` 统一 macOS 128B 事项仍未做（见 §5）。

### 4.2 明确不做（已守住边界）

- **不做跨进程传播 API**（env/UDS/spawn 注入）——传播是消费方策略，塞进传输层越界；
  且 STRACE-DESIGN 已有完整方案（薄壳 env / INHERIT_PARENT / 宿主 VM 回调 / donkey 捞取）。
- **不做 span_begin/span_end 生命周期**——记录是自包含行（dur 埋点处算好）；
  跨异步边界配对 begin/end 正是 Quill 架构刻意避开的复杂性。
- **不把 16B 内部结构烧进 xlog**——落地为不透明 `trace_id_hi`，CFS 的 ID 编码
  方案变更无需波及日志库。

### 4.3 与 strace/离线归并的衔接

- 四段埋点每行自带 16B trace_id（不透明，JSON 32-hex）+ `span_id`/`parent_span_id`
  顶层键 + `dur_ns`（TLV 字段），`strace_merge` 按 trace_id 过滤会话、按（时间戳，pid）
  排序、按 span/parent 重建调用树——数据结构足以支撑离线归并。

## 5. 裁决状态（2026-08-26 更新）

- [x] 16B 载体形态：**已落地** `log_context.trace_id_hi`（与现有字段对称，JSON 32-hex）。
- [x] `parent_span_id` / `dur_ns` 走向：**已落地** parent 为 JSON 顶层固定字段；
      dur 走 TLV 自定义字段（容量 4）。
- [x] `LOG_MAX_CUSTOM_FIELDS`：**已落地** 4（记录 320B / 5 缓存行，前端零可测回归）。
- [ ] CFS-DESIGN / STRACE-DESIGN 更新，注明「xlog 承载传输与格式化，
      tracing 语义（ID 生成/传播/建树）留在 CFS」。
- [ ] （独立事项，非本包范围）`CACHE_LINE_SIZE` 与 platform.h 的
      `XLOG_CACHE_LINE_SIZE` 统一（macOS 128B 行，防热索引伪共享）。
- [x] `rb_push` 的 TSan data race：**已修复**（memcpy 跳过 atomic `ready`，
      release 发布交给 `rb_commit`）。
