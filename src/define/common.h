#pragma once

#include <cstdint>
#include <atomic>

namespace IpcInterface {
namespace Define {
// 每一个Bit为1的枚举值
enum : uint32_t {
    BIT0 = 1u << 0,
    BIT1 = 1u << 1,
    BIT2 = 1u << 2,
    BIT3 = 1u << 3,
    BIT4 = 1u << 4,
    BIT5 = 1u << 5,
    BIT6 = 1u << 6,
    BIT7 = 1u << 7,
    BIT8 = 1u << 8,
    BIT9 = 1u << 9,
    BIT10 = 1u << 10,
    BIT11 = 1u << 11,
    BIT12 = 1u << 12,
    BIT13 = 1u << 13,
    BIT14 = 1u << 14,
    BIT15 = 1u << 15,
    BIT16 = 1u << 16,
    BIT17 = 1u << 17,
    BIT18 = 1u << 18,
    BIT19 = 1u << 19,
    BIT20 = 1u << 20,
    BIT21 = 1u << 21,
    BIT22 = 1u << 22,
    BIT23 = 1u << 23,
    BIT24 = 1u << 24,
    BIT25 = 1u << 25,
    BIT26 = 1u << 26,
    BIT27 = 1u << 27,
    BIT28 = 1u << 28,
    BIT29 = 1u << 29,
    BIT30 = 1u << 30,
    BIT31 = 1u << 31,
};

// shm_open 名称前缀（Linux 下对象在 /dev/shm/，不能改目录，只能约定名字）
constexpr const char* kPrefix = "/ipc_";

// 各进程消息队列共享内存名（下标可与进程角色对应）
constexpr const char* kShmNames[] = {
    "/ipc_daemon",
    "/ipc_ui",
    "/ipc_worker",
};

constexpr uint32_t kShmNameCount = sizeof(kShmNames) / sizeof(kShmNames[0]);

// 便于按名字取用
constexpr const char* Daemon = kShmNames[0];
constexpr const char* UI     = kShmNames[1];
constexpr const char* Worker = kShmNames[2];

// 各个进程之间的执行方式名称
constexpr const char* kProcessExecutableNames[] = {
    "./daemon",
    "./ui",
    "./worker",
};

constexpr uint32_t kProcessExecutableNameCount = sizeof(kProcessExecutableNames) / sizeof(kProcessExecutableNames[0]);

constexpr const char* DaemonExecutableName = kProcessExecutableNames[0];
constexpr const char* UIExecutableName = kProcessExecutableNames[1];
constexpr const char* WorkerExecutableName = kProcessExecutableNames[2];

// 下面的使用枚举值来代替各个进程的名称
enum{
    Daemon_Fd,
    UI_Fd,
    Worker_Fd,
    INVALID_FD // 无效的fd
};

// 用于控制各个进程之间的同步，解决某些进程需要依赖某个进程执行一些初始化才能正常运行的问题
enum{
    // 进程未同步标志
    PROCESS_SYNC_FLAG_NONE = 0,
    // 进程同步完成标志
    PROCESS_SYNC_FLAG_DONE = 1,
};
typedef struct {
    std::atomic<uint8_t> daemon_sync_flag;
    std::atomic<uint8_t> ui_sync_flag;
    std::atomic<uint8_t> worker_sync_flag;
} ProcessSyncInfo;

// 进程同步结构体共享内存名称
constexpr const char* ProcessSyncShmName = "/ipc_process_sync";

} // namespace Define
} // namespace IpcInterface