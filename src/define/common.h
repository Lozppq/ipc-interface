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
    "/ipc_process_1",
    "/ipc_process_2",
    "/ipc_process_3",
};

constexpr uint32_t kShmNameCount = sizeof(kShmNames) / sizeof(kShmNames[0]);

// 便于按名字取用
constexpr const char* Daemon = kShmNames[0];
constexpr const char* Process1 = kShmNames[1];
constexpr const char* Process2 = kShmNames[2];
constexpr const char* Process3 = kShmNames[3];

// 各个进程之间的执行方式名称
constexpr const char* kProcessExecutableNames[] = {
    "./daemon",
    "./process_1",
    "./process_2",
    "./process_3",
};

constexpr uint32_t kProcessExecutableNameCount = sizeof(kProcessExecutableNames) / sizeof(kProcessExecutableNames[0]);

constexpr const char* DaemonExecutableName = kProcessExecutableNames[0];
constexpr const char* Process1ExecutableName = kProcessExecutableNames[1];
constexpr const char* Process2ExecutableName = kProcessExecutableNames[2];
constexpr const char* Process3ExecutableName = kProcessExecutableNames[3];

// 逻辑进程槽位（与 kShmNames 下标一致，用作 ProcessSyncInfo::flags 下标；不是系统 fd）
enum {
    Daemon_Fd = 0,
    Process1_Fd,
    Process2_Fd,
    Process3_Fd,
    INVALID_FD  // 哨兵，须等于 kShmNameCount
};

// 断言，确保各槽位下标与 kShmNames 下标一致，避免出现低级错误
static_assert(Daemon_Fd == 0, "Daemon_Fd must be 0");
static_assert(Process1_Fd == 1, "Process1_Fd must match kShmNames[1]");
static_assert(Process2_Fd == 2, "Process2_Fd must match kShmNames[2]");
static_assert(Process3_Fd == 3, "Process3_Fd must match kShmNames[3]");
static_assert(INVALID_FD == kShmNameCount, "INVALID_FD must equal kShmNameCount");
static_assert(kShmNameCount == kProcessExecutableNameCount,
              "shm name count must match executable name count");

// 用于控制各个进程之间的同步，解决某些进程需要依赖某个进程执行一些初始化才能正常运行的问题
enum{
    // 进程未同步标志
    PROCESS_SYNC_FLAG_NONE = 0,
    // 进程同步完成标志
    PROCESS_SYNC_FLAG_DONE = 1,
};

// flags[Daemon_Fd / Process1_Fd / ...] 表示对应槽位是否同步完成
typedef struct {
    std::atomic<uint8_t> m_flags[kShmNameCount];
} ProcessSyncInfo;

// 同步标志初始化值数组
constexpr uint8_t kProcessSyncFlagInitValues[] = {
    PROCESS_SYNC_FLAG_DONE,
    PROCESS_SYNC_FLAG_DONE,
    PROCESS_SYNC_FLAG_DONE,
    PROCESS_SYNC_FLAG_DONE,
};

static_assert(sizeof(kProcessSyncFlagInitValues) == kShmNameCount, "kProcessSyncFlagInitValues must match kShmNameCount");

// 进程同步结构体共享内存名称
constexpr const char* ProcessSyncShmName = "/ipc_process_sync";

} // namespace Define
} // namespace IpcInterface