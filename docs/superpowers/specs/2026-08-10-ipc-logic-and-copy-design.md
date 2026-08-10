# ipc-interface 逻辑修复与传参优化设计

**日期:** 2026-08-10  
**范围:** `E:\ipc\ipc-interface`  
**前提:** 头文件已统一为 `Common.h`；分片结束条件、ReceiveWork 停机死锁、队列满日志等 Critical 已落地。

## 目标

分两阶段改进：

1. **阶段 1 — 逻辑正确：** 覆盖复审剩余 Important（按已确认取舍）。
2. **阶段 2 — 传参适中：** 消除明显多余拷贝，不改大消息架构。

成功标准：

- 固定通道崩溃后可重开 flag 并尽量继续环内未完成数据。
- 动态通道崩溃后 unlink，通知对端 unlink，新进程再申请。
- 发送失败最多重试 5 次；停机后不再重试。
- 协议畸形包不打崩 daemon；同名 ALLOCATE 有应答。
- 只读参数少拷贝；sink 路径 `std::move` 下沉。

## 方案选择

采用 **两阶段落地（方案 1）**：先合逻辑，再合传参，便于审阅与回滚。

不采用单 PR 全做、也不缩减为“只改热点路径”。

## 阶段 1：逻辑正确

### 停机与 wakeup

`ReceiveWork::stop` 顺序：

1. `setRunning(false)` 并唤醒 eventfd  
2. `wakeup_recv()`：`flag.fetch_and(~BIT1)` + `sem_post`（不清 BIT0）  
3. `wait()` join  

周期定时器在 `!isRunning()` 时不重插（已有）。

### 崩溃恢复

沿用并钉死现有分支语义：

| 通道类型 | 行为 |
|----------|------|
| 固定通道 (`logic_process_id != INVALID_FD`) | `set_flag(0)`，**保留环数据**；进程拉起后 `set_flag(BIT0\|BIT1)` |
| 动态通道 | 组 RELEASE → `handleDaemonMessage`：停收发线程、`delete_shm`/`unlink`，按现有逻辑通知对端 unlink；业务进程起来后再次 `RequestAllocateShm` |

不实现通用任务持久化库。

### 发送重试

- `TagSendMessage` 增加 `retry_count`（默认 0）。  
- 宏 `kSendMaxRetry = 5`（定义在 `TagMessage.h`）。  
- `SendWork::SendMessage`：`send` 失败则 `retry_count++`，若 `<= kSendMaxRetry` 则 `post` 回本线程再执行；否则 `LOG_ERROR` 丢弃。  
- `!isRunning()` 时直接 return，不重试。  
- 不在 `send_impl` 内自旋。

### 协议与所有权

- ALLOCATE/RELEASE：解析前校验 `tag->data.size()`（最小头 + `shm_name_len` 及后续字段）。  
- 同名已存在 ALLOCATE：**幂等回成功应答**。  
- `open(create=true)`：仅 `O_EXCL` 成功并完成初始化时 `is_owner_=true`；附着已有对象为 `false`。  
- `recv_impl` 非法 `total_len`：清 commit，`head=(head+1)%slot_count_`，本地 head 同步推进。

### 进程拉起与 Demo

- 进程同步：保留 sync SHM；启动全槽置 `DONE` 作为放行；**不实现真实分槽握手**（注释标明占位）。  
- `createProcess` 失败：宏 `kCreateProcessMaxRetry=50`，超出停止 `postTimer` 并打错误日志。  
- Demo：`setReceiveHandler` 处理 `MESSAGE_ID_PROCESS`；README 与 `Common.h` 一致。

## 阶段 2：传参适中（B）

### 规则

1. 只读、不获所有权 → `const std::string&` / `const std::shared_ptr<T>&`。  
2. 入队/下沉所有权 → 按值 + 链路 `std::move`。  
3. `send(std::vector<uint8_t> msg, …)` 保持 sink；调用方 `std::move`。  
4. 不拆掉 `TagSendMessage` 的 `shared_ptr`，不改零拷贝环形 API。

### 优先改动点

- `ProcessManager::createProcess` / `isAllowCreateProcess`  
- `ShmManager::postCreate*` / `createReceiveWork` / `createSendWork` / `Request*` 的 string 链路  
- `SendWork::SendMessage`：减少无意义的 `shared_ptr` 拷贝（`const&` 或 move 一次）

## 明确不做（YAGNI）

- 固定通道 unlink 重建  
- 真实多槽进程同步握手  
- 发送失败无限重试或独立重试线程  
- 大消息路径架构重写  

## 测试建议

1. 双进程互发：确认不再靠超时收齐单片消息。  
2. 释放动态 SHM / 停 ReceiveWork：不卡死。  
3. 固定通道杀进程再拉起：环内未消费数据仍可继续（在 flag 恢复后）。  
4. 动态通道杀发送端：unlink + 对端收到 RELEASE；重启后重新 ALLOCATE 可再通。  
5. 人为环满或关 BIT0：观察最多 5 次重试日志后丢弃。  
6. 畸形 ALLOCATE 包：daemon 不崩。  

## 落地顺序

1. 阶段 1 按模块改：停机/wakeup → 发送重试 → 协议/owner/recv head → 崩溃注释钉死 → createProcess 上限 → demo。  
2. 阶段 1 验证通过后再做阶段 2 传参。  
3. 实现前用 `/sp-write-plan` 拆任务。
