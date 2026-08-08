# ipc-interface

基于 POSIX 共享内存的进程间消息通道库（Linux）。守护进程创建/托管环形队列，业务进程通过 `ShmManager` 收发消息；支持子进程崩溃后回收与拉起。

## 架构概览

| 角色 | 槽位枚举 | 共享内存名 | 可执行文件 | 说明 |
|------|----------|------------|------------|------|
| Daemon | `Daemon_Fd` | `/ipc_daemon` | `daemon` | 创建各进程消息队列 shm，拉起子进程，`waitpid` 监控崩溃 |
| Process1 | `Process1_Fd` | `/ipc_process_1` | `process_1` | demo：周期性向 Process2 发消息 |
| Process2 | `Process2_Fd` | `/ipc_process_2` | `process_2` | demo：周期性向 Process1 发消息 |

名称与可执行文件定义在 `src/define/common.h`：`kShmNames[]`、`kProcessExecutableNames[]`，下标与 `Daemon_Fd` / `ProcessN_Fd` 对齐。

另有进程同步 shm：`/ipc_process_sync`（`ProcessSyncInfo`）。其中 `flags[槽位]` 表示该槽是否允许拉起；`ProcessManager` 用 shm 名解析槽位后读 `flags[fd]`。

主要模块：

- `ShmManager`：本进程消息收发、打开/创建 `StreamShmCreator` 队列
- `ProcessManager`：fork/exec 子进程、崩溃后重新拉起
- `StreamShmCreator` / `ShmCreator`：POSIX shm 环形队列与通用映射模板
- `MessageThread`：无锁任务队列 + 定时器工作线程
- `Log_Print`：`LOG_INFO` / `LOG_ERROR` 等

## 编译

依赖：g++（C++14）、pthread、librt。仅支持 Linux（依赖 `shm_open` / `fork` / `waitpid` 等）。

```bash
cd ipc-interface
make          # 生成 lib、daemon、demo
make clean
```

交叉编译示例：

```bash
make CROSS_COMPILE=aarch64-linux-gnu-
```

产物：

```text
build/include/           # 头文件树（与 src 对应）
build/lib/libipc-interface.so
build/bin/daemon
build/bin/process_1
build/bin/process_2
```

## 运行 demo

`ProcessManager` 按 `kProcessExecutableNames` 拉起 `./process_1`、`./process_2`，需在 `build/bin` 下启动 daemon：

```bash
cd build/bin
./daemon
```

daemon 在同步 shm 就绪后拉起 `process_1`、`process_2`。两端互相 `send`，日志中可看到收发内容。

若需单独调试某个业务进程（shm 已由 daemon 创建）：

```bash
cd build/bin
./process_1
# 另一个终端
./process_2
```

## 业务进程接入要点

```cpp
#include "mul_process/ShmManager.h"
#include "define/Common.h"
#include "log/Log_Print.h"

int main() {
    auto* mgr = IpcInterface::MulProcess::ShmManager::getInstance();
    mgr->initParams(IpcInterface::Define::Process1);  // 或 Process2 / Daemon
    mgr->start();

    std::vector<uint8_t> msg = {/* ... */};
    mgr->send(msg, IpcInterface::Define::Process2);  // 发到对端队列名

    mgr->wait();  // 或自行保活
    return 0;
}
```

- `initParams(本进程队列名)`：非 daemon 会登记所有 `kShmNames`，便于打开发送目标队列
- `send(msg, 目标 shm 名)`：写入对端的接收环
- 收消息在 `ShmManager::onReceiveMessage`（当前 demo 实现为打日志；可按业务改）

### 增加新业务进程

在 `common.h` 中按同一下标同步扩展（插在 `INVALID_FD` 之前）：

1. `kShmNames` 增加 `/ipc_process_N`
2. `kProcessExecutableNames` 增加 `./process_N`
3. 枚举增加 `ProcessN_Fd`
4. 如有对应别名常量（`ProcessN`）一并补上
5. 在 `demo/` 增加 `process_N.cpp`，`make` 后产物为 `build/bin/process_N`

编译期 `static_assert` 会检查槽位与表长度是否一致。

## 目录结构

```text
src/
  daemon/          # daemon 入口
  define/          # common.h（名称、槽位、同步结构）、MessageId.h
  log/             # 日志
  model/           # ThreadBase / MessageThread / LockFreeQueue / ShmCreator
  mul_process/     # ShmManager / ProcessManager / StreamShmCreator / Send&ReceiveWork
demo/
  process_1.cpp
  process_2.cpp
Makefile
```

## 说明

- 共享内存对象在 `/dev/shm/`，名称以 `/` 开头（如 `/ipc_process_1`）
- 同步位访问：`ProcessSyncInfo::flags[Define::Process1_Fd]`（槽位枚举，不是系统 fd）
- 槽位超时等策略见 `StreamShmCreator.h` 中 `TIMEOUT_*`
- Windows 下仅便于浏览代码；完整功能请在 Linux / WSL 编译运行
