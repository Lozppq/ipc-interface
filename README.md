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

**环形共享内存模式**：只支持**单接收者、多发送者**（1 个 reader，N 个 writer）。同一环上不要挂多个接收线程/进程；多路并发接收需各自申请独立通道。

## 编译

依赖：g++（C++14）、pthread、librt。仅支持 Linux（依赖 `shm_open` / `fork` / `waitpid` 等）。

```bash
cd ipc-interface
make          # 生成 lib、daemon、demo、对照测试
make tests    # 只编 test/（如 udp_process）
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
build/bin/process_3   # 随 demo/*.cpp 自动生成
build/bin/udp_process    # 本机 UDP 对照，make / make tests 生成
```

## 运行 demo

必须在**源码根目录**下进入产物目录再启动守护进程（`ProcessManager` 用相对路径 `./process_N` 拉起子进程，工作目录不对会找不到可执行文件）：

```bash
cd /path/to/ipc-interface   # 源码根目录
make                        # 若尚未编译
cd build/bin
./daemon &
```

`./daemon &` 后台运行后，守护进程会创建固定环形 shm / 同步 shm，再按 `kProcessExecutableNames` **自动拉起** `process_1`、`process_2`、`process_3` 等业务进程，无需再手动逐个启动。

进程间通过各自固定 inbox 互发 `MESSAGE_ID_PROCESS`；日志中可看到收发与 `setReceiveHandler` 回调。

## 性能

双核 CPU、同条件小包互发（约 1KB 载荷、三进程互相收发）下：

| 通道 | 吞吐 |
|------|------|
| 本机 Unix DGRAM（`SOCK_DGRAM`） | 约 **20 MB/s** |
| 共享内存 64 字节槽 | 约 **40 MB/s** |
| 共享内存 1KB 槽 | 接近 **60 MB/s** |

槽位越大，分片越少，吞吐大致按倍数上升。共享内存软中断基本为 **0**，内核态:用户态 CPU 占用比约 **6.5:3.5**；Unix DGRAM 软中断约占 **20%** CPU，抢占调度更高。

对照程序是 `test/udp_process.cpp`，载荷与 `demo/process_1/2/3` 相同（`n∈[1,1000]` 个 `uint16`，不发给自己），走 `127.0.0.1` UDP，端口 `51001~51003`。三个进程都要起，少一个则有一半包打到空端口，数字会对不齐。

```bash
cd /path/to/ipc-interface
make tests                  # 或 make
cd build/bin
./udp_process 1             # 三个终端各跑一个
./udp_process 2
./udp_process 3
```

日志前缀为 `udp_1` / `udp_2` / `udp_3`，每秒打印 `recv rate`（实际收到）和 `send`（本进程发出）。UDP 可能静默丢包，以 `recv` 与 demo 的 `recv rate` 对比。

若需单独调试某个业务进程（shm 已由 daemon 创建）：

```bash
cd /path/to/ipc-interface/build/bin
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
6. `kProcessSyncFlagInitValues` 增加对应初值（见下节）

编译期 `static_assert` 会检查槽位与表长度是否一致。

### 进程同步（`Common.h`）

部分业务进程要等别的进程初始化完才能拉起时，用 `/ipc_process_sync`（`ProcessSyncInfo`）做槽位级门闩。定义都在 `src/define/Common.h`。

- `m_flags[Daemon_Fd / ProcessN_Fd]`：该槽是否允许 daemon 拉起对应可执行文件（**槽位枚举，不是系统 fd**）
- `PROCESS_SYNC_FLAG_NONE`（0）：未就绪，不拉起；`PROCESS_SYNC_FLAG_DONE`（1）：允许拉起
- `kProcessSyncFlagInitValues[]`：daemon 创建同步 shm 时的初值，下标必须与 `kShmNameCount` 一致
- `ProcessSyncShmName`：`/ipc_process_sync`

`ProcessManager` 只在 `flags[槽位] == DONE` 时 `fork/exec`；为 `NONE` 时每 100ms 重试，直到被置为 `DONE`。崩溃后重新拉起也走同一检查。

**延迟拉起某个进程**：把该槽初值改成 `NONE`，依赖方就绪后再置 `DONE`。例如希望 `process_3` 等 `process_1` 初始化完再启动：

```cpp
// Common.h：process_3 初始不拉起
constexpr uint8_t kProcessSyncFlagInitValues[] = {
    PROCESS_SYNC_FLAG_DONE,  // Daemon
    PROCESS_SYNC_FLAG_DONE,  // Process1
    PROCESS_SYNC_FLAG_DONE,  // Process2
    PROCESS_SYNC_FLAG_NONE,  // Process3，等别人置 DONE
};
```

`process_1` 初始化完成后通知 daemon（任意线程用 `postSetSyncFlag`）：

```cpp
#include "mul_process/ShmManager.h"
#include "define/Common.h"

auto* mgr = IpcInterface::MulProcess::ShmManager::getInstance();
// 允许拉起 / 重新拉起 process_3
mgr->postSetSyncFlag(IpcInterface::Define::Process3,
                     IpcInterface::Define::PROCESS_SYNC_FLAG_DONE);
```

消息经 `MESSAGE_ID_DAEMON` / `MESSAGE_SUB_ID_SET_SYNC_FLAG` 到 daemon，再 `ProcessManager::setProcessSyncFlag`。也可把已在跑的槽改回 `NONE`，之后崩溃将不会被自动拉起，直到再次 `DONE`。

默认四槽全是 `DONE`，与现在 demo 启动即拉起全部业务进程一致。

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
test/
  udp_process.cpp   # 本机 UDP 吞吐对照
Makefile
```

## 说明

- **环形队列（`StreamShmCreator`）仅支持单接收者多发送者**：一个共享内存环只允许一个接收端消费；发送端可以有多个。不支持多接收者争用同一环；若需一对多广播或扇出，应为每个接收者创建独立通道。
- 共享内存对象在 `/dev/shm/`，名称以 `/` 开头（如 `/ipc_process_1`）
- 同步位访问：`ProcessSyncInfo::m_flags[Define::Process1_Fd]`（槽位枚举，不是系统 fd）；用法见上文「进程同步」
- 槽位超时等策略见 `StreamShmCreator.h` 中 `TIMEOUT_*`
- Windows 下仅便于浏览代码；完整功能请在 Linux / WSL 编译运行
