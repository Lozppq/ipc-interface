# ipc-interface

基于 POSIX 共享内存的进程间消息通道库（Linux）。守护进程创建/托管环形队列，业务进程通过 `ShmManager` 收发消息；支持子进程崩溃后回收与拉起。

## 架构概览

| 角色 | 槽位枚举 | 共享内存名 | 可执行文件 | 说明 |
|------|----------|------------|------------|------|
| Daemon | `Daemon_Fd` | `/ipc_daemon` | `daemon` | 创建各进程消息队列 shm，拉起子进程，`waitpid` 监控崩溃 |
| Process1 | `Process1_Fd` | `/ipc_process_1` | `process_1` | demo：周期性向 Process2 发消息 |
| Process2 | `Process2_Fd` | `/ipc_process_2` | `process_2` | demo：周期性向 Process1 发消息 |

名称与可执行文件定义在 `src/define/Common.h`：`kShmNames[]`、`kProcessExecutableNames[]`，下标与 `Daemon_Fd` / `ProcessN_Fd` 对齐。

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

daemon 在同步 shm 就绪后拉起 `process_1`、`process_2`。两端互相 `send`，日志中可看到 `send` 与 `setReceiveHandler` 回调里的 `recv message_id=...`。

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
#include "define/MessageId.h"
#include "log/Log_Print.h"

int main() {
    auto* mgr = IpcInterface::MulProcess::ShmManager::getInstance();
    mgr->initParams(IpcInterface::Define::Process1);  // 或 Process2 / Daemon
    mgr->start();
    mgr->setReceiveHandler([](std::shared_ptr<IpcInterface::MulProcess::TagReceiveMessage> tag) {
        if (!tag) return;
        // 处理 MESSAGE_ID_PROCESS 业务消息
    });

    std::vector<uint8_t> msg = {/* ... */};
    mgr->send(std::move(msg), IpcInterface::Define::MESSAGE_ID_PROCESS,
              IpcInterface::Define::Process2);  // 移交所有权后异步发送

    mgr->wait();  // 或自行保活
    return 0;
}
```

- `initParams(本进程队列名)`：非 daemon 会登记所有 `kShmNames`，便于打开发送目标队列
- `send(msg, message_id, 目标 shm 名)`：写入对端的接收环；业务互通用 `MESSAGE_ID_PROCESS`
- `setReceiveHandler`：注册**本进程固定 inbox**上 `MESSAGE_ID_PROCESS` 的回调；daemon 协议走内部 `onReceiveMessage`

### 动态申请 / 释放共享内存

固定通道（`/ipc_daemon`、`/ipc_process_1`…）由 daemon 启动时创建。业务之间若需要**额外**环形队列，向 daemon 申请动态 SHM。

#### 流程概览

```text
业务进程                         Daemon
   |  postRequestAllocateShm(...)    |
   |------ MESSAGE_ID_DAEMON ------->|
   |  (ALLOCATE 子消息)               | 创建 POSIX shm，登记 PidNameInfo
   |<----- 同一 ALLOCATE 回包 --------|  回给 sender / receiver 双方
   |  自动 open(false) 挂接           |
   |  接收端再 postCreateReceiveWork  |
   |  之后 send(..., new_shm_name)    |
```

`postRequestAllocateShm` / `RequestAllocateShm` 返回 `true` 只表示**请求已投递到 daemon**，不表示环已建好。挂接在收到 daemon 回包后由内部 `handleProcessMessage` 完成。

#### 申请（推荐跨线程用 post 接口）

```cpp
#include "mul_process/ShmManager.h"
#include "mul_process/StreamShmCreator.h"  // SIZE_64B / SIZE_1KB / SIZE_256KB
#include "define/Common.h"

auto* mgr = IpcInterface::MulProcess::ShmManager::getInstance();

// 参数含义：
//   sender_shm_name   — 发送侧进程的固定 inbox 名（如 Define::Process1）
//   receiver_shm_name — 接收侧进程的固定 inbox 名（如 Define::Process2）
//   slot_size         — 单槽字节数，必须是 SIZE_64B / SIZE_1KB / SIZE_256KB 之一
//   slot_count        — 槽个数（如 1024）
//   new_shm_name      — 新通道名，必须以 '/' 开头，且不在 kShmNames 固定表中
//                       （如 "/ipc_dyn_p1_to_p2"）
mgr->postRequestAllocateShm(
    IpcInterface::Define::Process1,
    IpcInterface::Define::Process2,
    IpcInterface::MulProcess::SIZE_64B,
    1024,
    "/ipc_dyn_p1_to_p2");
```

若已在 `ShmManager` 工作线程内，也可直接调私有路径对应的投递；对外请用 **`postRequestAllocateShm`**（任意线程安全投递）。

注意：

- `new_shm_name` 长度需能放进协议里的 `u8` 长度字段（建议短名）。
- 同名已存在时，本进程 `RequestAllocateShm` 会失败返回；daemon 侧对重复申请会**幂等回包**，不重复创建。
- 发送方逻辑 id / 接收方逻辑 id 由 `sender_shm_name`、`receiver_shm_name` 在本地表里解析，须是已 `initParams` 登记过的固定进程名。

#### 挂接成功后：收发

双方在收到 ALLOCATE 回包后会 `open(false)` 并把通道放进 `shmInfosMap_`。

**发送**（通道名用动态名，不是固定 Process2 inbox）：

```cpp
std::vector<uint8_t> msg = {/* ... */};
mgr->send(std::move(msg),
          IpcInterface::Define::MESSAGE_ID_PROCESS,
          "/ipc_dyn_p1_to_p2");
```

默认 `SendWork` 即可发送；若要为该通道单独发送线程：

```cpp
mgr->postCreateSendWork("/ipc_dyn_p1_to_p2");
```

**接收**（动态通道**不会**走 `setReceiveHandler` 那个固定 inbox；接收端必须另建 ReceiveWork）：

```cpp
mgr->postCreateReceiveWork(
    "/ipc_dyn_p1_to_p2",
    [](std::shared_ptr<IpcInterface::MulProcess::TagReceiveMessage> tag) {
        if (!tag) return;
        // 处理该动态通道上的业务消息
    });
```

一般由**接收侧进程**在认为通道将就绪后调用；若 open 尚未成功，内部会定时重试创建 ReceiveWork。

#### 释放

```cpp
mgr->postRequestReleaseShm("/ipc_dyn_p1_to_p2");
```

流程：向 daemon 发 RELEASE → daemon `unlink` 并通知相关进程 → 各进程停该通道的 Send/ReceiveWork 并 `close`。  
崩溃时：固定通道只清 flag、保留环数据；**动态通道**会走 RELEASE/`unlink`，进程起来后需业务再次 `postRequestAllocateShm`。

#### 与固定通道对比

| | 固定通道 | 动态通道 |
|--|----------|----------|
| 创建 | daemon 启动创建 | 业务 `postRequestAllocateShm` |
| 名称 | `kShmNames[]` | 自定义 `/...`，勿与固定名冲突 |
| 收消息 | `setReceiveHandler` | `postCreateReceiveWork` |
| 发消息 | `send(..., ProcessN)` | `send(..., new_shm_name)` |
| 释放 | 一般不释放 | `postRequestReleaseShm` |

### 增加新业务进程

在 `Common.h` 中按同一下标同步扩展（插在 `INVALID_FD` 之前）：

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
  define/          # Common.h（名称、槽位、同步结构）、MessageId.h
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
