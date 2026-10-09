# 外部进程报到与 pidfd 崩溃监视

**日期:** 2026-10-08  
**范围:** `src/define/Common.h`、`MessageId.h`、`EpollControl`、`MessageThread`、`ShmManager`、`ProcessManager`、`Daemon.cpp`  
**不改:** 环形队列占有协议、已删除的心跳、协议层 BusClient

## 目标

`m_executable` 为空时 daemon 不 fork，业务自己打开 inbox。daemon 不是父进程，`waitpid(-1)` 收不到退出。需要：

1. 业务在本进程 inbox 打开成功后立刻向 daemon 报到（逻辑槽位 + 真实 pid）。
2. 仅对「不是 daemon fork 出来的」进程用 pidfd + epoll 监视退出。
3. `kProcesses` 两个开关：`m_boot` 开机是否 daemon fork；`m_restart` 仅开机不拉起时，pidfd 退出后是否 daemon fork。任何 fork 都须 `m_executable` 非空。

成功标准：

- 开机拉起：仅 `m_boot && executable 非空`；崩溃走 `waitpid` 再拉。
- 开机不拉起：`PROCESS_ONLINE` + pidfd；`m_restart && executable 非空` 则退出后 daemon fork，否则只 shm 清理。
- 同一槽重复报到：替换旧 pidfd，不重复监视。
- 不把 pidfd 加到 ShmManager 的 epoll 上。

## 方案选择

采用 **pidfd_open + ProcessManager 线程上的 EpollControl 通用 fd 回调**。

不采用：定时扫 `/proc/<pid>`（pid 复用）；不在 `Daemon.cpp` 的 `waitpid` 循环旁再开一套 epoll。

`waitpid` 循环保留，只收 daemon 子进程。

内核：Linux ≥ 5.3。`pidfd_open` 失败只打日志，该槽不监视。

## 报到消息

`MESSAGE_SUB_ID_PROCESS_ONLINE`，`MESSAGE_ID_DAEMON`，小端：

`[u16 sub][u8 logic_id][u32 os_pid]`（7 字节）。

发送方：非 daemon 的业务进程。时机：`OnThreadInit` → `initShm` → **本进程 inbox `Open` 成功且 `initReceiveWork` 已调用之后立刻发**。`logic_id` = `getLogicProcessId(m_shm_name)`，`os_pid` = `getpid()`。发送失败允许 1s 重试（与 open 重试同档），避免 daemon inbox 尚未可写。

daemon 不发此消息。

## daemon 处理

`handleDaemon_ProcessOnline` 只解析并 `ProcessManager::postProcessOnline(logic_id, os_pid)`。禁止在 ShmManager 工作线程里 `pidfd_open` / `epoll_ctl`。

`postProcessOnline` 在 ProcessManager 工作线程执行：

| 开机是否拉起（`m_boot && executable 非空`） | 行为 |
|--|--|
| 是 | 丢弃报到。该槽由 fork + `waitpid` 跟踪，禁止再挂 pidfd。 |
| 否 | 外部进程：见下 |

外部进程：

1. 该槽已有 `ProcessInfo.m_fd >= 0`：`removeFd` + `close` 旧 pidfd，去掉旧记录。
2. `pidfd_open(os_pid, 0)`，失败则 return。
3. 写入 `ProcessInfo`：`m_shm_name`、`m_pid`、`m_fd`（pidfd）、`m_process_executable_name` 可空。
4. `MessageThread::addFd(pidfd, callback)`（须在工作线程内，规则同 `startTimer`）。
5. `ShmManager::enableChannel(该槽 shm 名)`，否则崩溃清 flag 后外部进程无法再发。

## EpollControl / MessageThread

现有 `wait()` 对非 eventfd、非 timer 的 fd 直接跳过。增加：

- `using FdCallback = std::function<void(int fd)>`
- `bool addFd(int fd, FdCallback cb)`：`epoll_ctl ADD`，`EPOLLIN`
- `void removeFd(int fd)`：`epoll_ctl DEL`，**不 close**（调用方 close）

`wait()` 顺序：eventfd 排任务队列 → timer 走现逻辑 → 否则 `m_fds[fd](fd)`。

`MessageThread` 转发 `addFd` / `removeFd`（`m_epoll` 保持 private）。跨线程调用时 `post` 到工作线程再 add/del。

pidfd 可读：进程已退出。回调内：

- 不对外部 pid 调用 `waitpid`（不是子进程）。
- `removeFd` + `close(pidfd)`。
- 与 `waitpid` 入口汇合到同一套崩溃处理（见下）。

## ProcessInfo

增加 `int m_fd{-1}`。daemon 子进程为 `-1`（只走 `waitpid`）。仅外部报到成功的记录 `m_fd >= 0`。

## ProcessDesc

```text
const char* m_shm_name;
const char* m_executable;  // 空 / nullptr：任何路径都不 fork
bool        m_boot;        // 开机是否 daemon fork；true 则 waitpid 崩溃再拉
bool        m_restart;     // 仅 m_boot==false：pidfd 退出后是否 daemon fork
uint8_t     m_sync_flag;
```

| | `m_boot && exe 非空` | `!m_boot && m_restart && exe 非空` | 否则 |
|--|--|--|--|
| 启动 | `postCreateProcess` | 不 fork，等 ONLINE | 不 fork，等 ONLINE |
| 崩溃 | `waitpid` 后再拉 | pidfd 后再 `createProcess` | pidfd；只 shm 清理 |

现有 demo 三进程：`m_boot = true`，`m_restart = false`。外部要接手：`m_boot = false`，`m_restart = true`，命令非空。

`initReceiveWork` 重试、同步位 `NONE` 的 1s 重试逻辑不变；空 executable 仍不创建、不重试拉起。

## 崩溃处理（两条入口汇合）

```text
waitpid（子进程）或 pidfd 可读（外部）
  → ShmManager::handleProcessCrash(logic_id)   // 固定通道清 flag 留环；动态通道 RELEASE
  → daemon 子进程（waitpid、ProcessInfo 带 executable 且无 pidfd）再 createProcess
     外部进程：m_restart && executable 非空则 createProcess，否则不 fork
  → 删除该 ProcessInfo，确保 pidfd 已摘 epoll 并 close
```

`m_restart` 接手后的新进程是 daemon 子进程，之后崩溃走 `waitpid`。未接手的外部进程再次自行启动会再发 `PROCESS_ONLINE`。

`Daemon.cpp` 的 `waitpid` 循环保留。`isNeedActivePullProcess` 继续用于过滤无关 pid。

## 边界

- pid 复用：监视对象是 pidfd，不是裸 pid。
- 同槽新 pid 报到：替换监视。
- 逻辑槽位非法（`>= kShmNameCount` 或 daemon 槽）：丢弃报到。
- 不恢复心跳。

## 实现顺序（落地时）

1. `EpollControl` 通用 fd + `MessageThread` 转发  
2. `ProcessDesc.m_boot` / `m_restart`、`ProcessInfo.m_fd`  
3. `PROCESS_ONLINE` 发送与 daemon 分发  
4. `postProcessOnline` + pidfd + `enableChannel`  
5. 开机认 `m_boot`；pidfd 退出认 `m_restart`  
6. README：空 executable、报到、两个开关

## 非目标

- 为 timer 和 pidfd 各维护一套 epoll。
- 在 ShmManager 线程里盯进程。
- 用 `/proc` 轮询代替 pidfd。
- 在 `m_restart == false` 时把外部进程改成 daemon 子进程。
