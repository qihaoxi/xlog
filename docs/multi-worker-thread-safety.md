# 多 worker（多消费者）并发下 xlog 非线程安全的实证结论

- 状态: 已实证（测试 + 4 种 sanitizer 全矩阵复核）
- 日期: 2026-09-04
- 触发: 外部测试反馈"xlog 在多 worker 场景下不是线程安全的"
- 相关: `tests/test_multi_worker.c`、`src/ringbuf.h`（MPSC 自述）

## 1. 结论一句话

**属实。** 非线程安全的路径是"多个 worker 线程并发 drain 同一个 ring buffer"
（消费侧多消费者），已用 TSan 捕获 159 条 data race，并且该场景不仅在数据竞争层面
损坏，还会直接造成**管线活锁（livelock）与记录重复/丢失**。公开 API 的多线程
**生产**侧（多生产者）是线程安全的，TSan 零告警。

## 2. 场景界定：多 worker 指什么

xlog 自述为 MPSC（`src/ringbuf.h:161`"Only ONE consumer thread may call
rb_peek/rb_consume or rb_pop"），后台只有单个消费线程（`xlog_add_sink` 懒启动）。
也就是说：

- 生产侧多线程（`xlog_log`/`xlog_log_ctx` → `rb_reserve`/`rb_commit`）：**支持**，MP。
- 消费侧多"worker"（外部直接拿内部 ring API 起多个线程并发 drain）：**不支持**，SC。

`tests/test_multi_worker.c` 把这两种意图都结构化了（场景 A/B），并断言行为；
单跑时用 `MW_BUDGET=<秒>` 控制墙钟预算，防止活锁把测试/CI 饿死。

## 3. 测试设计

- 场景 A（基线对照）：8 线程 × 5000 次 `xlog_log()` 并发生产（走公开 API + 计数 sink）。
- 场景 B（核心复现）：4 生产者 × 20000 条（每条 payload 唯一）+ 4 个 worker
  并发 `rb_peek`/`rb_consume`（zero-copy）与 `rb_pop`（copy-out）drain 同一 ring
  （DROP 策略）。压力跑完仍会在预算内判 **LIVELOCK**；join 后做载荷去重核对
  （重复=同一记录被多个 worker 处理，非法=撕裂读），任一 FAILED 退出码非 0。

## 4. 验证矩阵（x86-64 Linux，gcc 15.2 / clang 21.1）

| 配置 | 结果 | pushed / 20000×4 | processed | 重复载荷 | 说明 |
|------|------|------|------|------|------|
| **ASan**（gcc） | FAILED | 93 | 94 | 25 | 库侧无内存错误；竞态表现为活锁+重复 |
| **TSan**（gcc） | FAILED（159 race） | 80000 | 80002 | 693 | 全部 data race 在消费侧；场景 A 零告警 |
| **MSan**（clang） | FAILED | 16569 | 16572 | 7273 | 无未初始化读；近半数载荷重复 |
| **UBSan**（clang，bounds+undefined） | FAILED | 16457 | 16460 | 7326 | 无越界/UB（掩码索引进界）；重复同样严重 |

（`-fsanitize=bounds-strict` 在 clang 21 已并入 `-fsanitize=bounds`，见 §6。）

## 5. 根因分析

### 5.1 数据竞争（TSan：159 条，全部集中在消费路径）

`rb_peek`/`rb_consume`/`rb_pop` 之间没有任何互斥或"单消费者"强约束：

- 竞态典型形态 1：worker A 在 `rb_pop` 里 `memcpy(out_rec, rec, ...)`
  （`ringbuf.c:425`）**读** slot，同时 worker B 在 `rb_consume`→`log_record_reset`
  （`log_record.h:653,657,...`）里**写**同一 slot → 撕裂读/写。
- 竞态典型形态 2：两个 worker 同时对同一 slot 执行 `log_record_reset`
  （`log_record.h:654,661,665-671` 的 `fmt/arg_count/arg_values/...`）→ 写-写竞态。

### 5.2 功能性活锁（ASan/USan/MSan/plain 下 pushed 只有个位数~万级）

多消费者双双 peek 到同一 slot，再各自 `rb_consume`：

- `read_idx` 一次前进 **≥2 步**，而 `write_idx` 只进 1 步；
- 一旦 `rd > wr`，`rb_size()` 与 `rb_reserve()` 里的 `wr - rd` **无符号下溢**成巨数，
  生产者看到"永远满"（DROP/重试空转、饿死），消费者看到"永远空"（`rb_peek` 一直回
  NULL）→ **永久活锁**。

### 5.3 违背的并发约束

`ringbuf.h` 已用注释声明"only ONE consumer"，但 API 本身**没有强制**（无断言/无锁/
无消费权登签），使用者一旦多 worker 即触发 5.1+5.2。修复方向（供设计决策，未实施）：

- **方案①（最小，推荐做广告即可）**：保持 MPSC，在多消费者入口加硬断言/
  文档警示 + 公开 API 不暴露内部 ring（当前内部头 `src/ringbuf.h` 本就非公开，
  属内部使用越界）。
- **方案②（破坏性）**：把 ring 升级为 MPMC（消费侧 CAS 抢槽 + 双阶段 release），
  改动大、热路径可能受损——与"为极致性能而生的 MPSC"定位冲突，不建议贸然做。

## 6. 环境备注

- `-fsanitize=bounds-strict`：clang 21 报 `unsupported argument`，该选项已并入
  `-fsanitize=bounds`（含 array-bounds/local-bounds 的严格语义）；本次以
  `bounds,undefined -fno-sanitize-recover=all` 等价覆盖。
- TSan/MSan 需全量插桩（库 + 测试同构建）；ASan/UBSan 无此限制。

## 7. 复现

```bash
# 一次冒烟（预算 10s）
MW_BUDGET=10 ./cmake-build-debug/tests/test_multi_worker

# 四种 sanitizer 全矩阵
cmake -S . -B cmake-build-asan  -DENABLE_ADDRESS_SANITIZER=ON -DBUILD_EXAMPLES=OFF
cmake -S . -B cmake-build-tsan  -DENABLE_THREAD_SANITIZER=ON  -DBUILD_EXAMPLES=OFF
cmake -S . -B cmake-build-msan  -DENABLE_MEMORY_SANITIZER=ON  -DBUILD_EXAMPLES=OFF -DCMAKE_C_COMPILER=clang
cmake -S . -B cmake-build-ubsan -DENABLE_UNDEFINED_BEHAVIOR_SANITIZER=ON -DBUILD_EXAMPLES=OFF -DCMAKE_C_COMPILER=clang "-DCMAKE_C_FLAGS=-fsanitize=bounds,undefined -fno-sanitize-recover=all"

MW_BUDGET=15 ./cmake-build-{debug,asan,tsan,msan,ubsan}/tests/test_multi_worker
```

场景 A/B 的退出码见 §3；TSan 因检测到竞态以 66 退出。
