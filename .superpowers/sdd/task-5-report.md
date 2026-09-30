# Task 5 Report: Crash comments + createProcess retry cap + sync placeholder

## Status

**COMPLETE**（未 commit，按 dispatch 要求）

## Files

| File | Change |
|------|--------|
| `src/mul_process/ShmManager.cpp` | `handleProcessCrash`：固定通道 `set_flag(0)` 前补充环数据保留注释；动态通道 RELEASE 路径前补充解链/需重新 `RequestAllocateShm` 注释 |
| `src/mul_process/ProcessManager.h` | `createProcess` 声明增加 `int retry_n = 0` |
| `src/mul_process/ProcessManager.cpp` | 匿名命名空间 `kCreateProcessMaxRetry=50`；`createProcess` 失败/not-allow 分支 capped 重试；`initProcessSyncShm` 补充 bootstrap 占位注释（保留原注释） |

## Spot-check

**`ShmManager::handleProcessCrash`**
- 固定通道（`logic_process_id != INVALID_FD`）：仅 `set_flag(0)`，行为不变；新增注释说明环数据保留供 relaunch resume。
- 动态通道：仍走 `handleDaemonMessage` RELEASE 路径，行为不变；新增注释说明 SHM 解链、对端通知、业务须重新 `RequestAllocateShm`。

**`ProcessManager::createProcess`**
- 签名：`createProcess(shm_name, exe, retry_n=0)`，默认参数仅在头文件。
- `startProcess` 失败或 `!isAllowCreateProcess`：`retry_n >= 50` 时 `LOG_ERROR` give up 并 return；否则 100ms 后 `retry_n+1` 重试。
- `handleProcessCrash` / `initCreateProcess` 调用仍用默认 `retry_n=0`，计数从每次 create 链独立起算。

**`ProcessManager::initProcessSyncShm`**
- 循环仍对所有槽写 `PROCESS_SYNC_FLAG_DONE`；原注释保留，新增 bootstrap 占位说明（非真实 per-slot 握手）。

## Build

`SKIP_BUILD`（Windows 环境无 `make`/Linux 工具链）；改动为注释 + 参数/重试逻辑，无可见编译风险。

## Concerns

- 重试上限 50 次 × 100ms ≈ 5s 后放弃；若 sync SHM 长期未就绪或 exe 缺失，该进程槽将不再重试，需外部监控日志 `give up create after`。
- `initProcessSyncShm` 仍为 bootstrap 占位，真实 per-slot 握手未实现（与 Task 5 范围一致，非回归）。
