#pragma once

#include <cstdint>
#include <atomic>

namespace IpcInterface
{
namespace Define
{
// 每一个Bit为1的枚举值
enum : uint32_t
{
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

// 逻辑进程槽位（与 kProcesses 下标一致，用作 ProcessSyncInfo::m_flags 下标；不是系统 fd）
enum
{
    Daemon_Fd = 0,
    Process1_Fd,
    Process2_Fd,
    Process3_Fd,
    INVALID_FD  // 哨兵，须等于 kShmNameCount
};

// 用于控制各个进程之间的同步，解决某些进程需要依赖某个进程执行一些初始化才能正常运行的问题
enum
{
    // 进程未同步标志
    PROCESS_SYNC_FLAG_NONE = 0,
    // 进程同步完成标志
    PROCESS_SYNC_FLAG_DONE = 1,
};

// 进程槽位：共享内存名、可执行文件、同步初值。下标与上面的槽位枚举一致
typedef struct
{
    const char* m_shm_name;
    const char* m_executable;
    uint8_t m_sync_flag;
} ProcessDesc;

constexpr ProcessDesc kProcesses[] = {
    { "/ipc_daemon", "./daemon", PROCESS_SYNC_FLAG_DONE },
    { "/ipc_process_1", "./process_1", PROCESS_SYNC_FLAG_DONE },
    { "/ipc_process_2", "./process_2", PROCESS_SYNC_FLAG_DONE },
    { "/ipc_process_3", "./process_3", PROCESS_SYNC_FLAG_DONE },
};

constexpr uint32_t kShmNameCount = sizeof(kProcesses) / sizeof(kProcesses[0]);
static_assert(INVALID_FD == kShmNameCount, "INVALID_FD must equal kShmNameCount");

constexpr const char* Daemon = kProcesses[Daemon_Fd].m_shm_name;
constexpr const char* Process1 = kProcesses[Process1_Fd].m_shm_name;
constexpr const char* Process2 = kProcesses[Process2_Fd].m_shm_name;
constexpr const char* Process3 = kProcesses[Process3_Fd].m_shm_name;

// m_flags[Daemon_Fd / Process1_Fd / ...] 表示对应槽位是否同步完成
typedef struct
{
    std::atomic<uint8_t> m_flags[kShmNameCount];
} ProcessSyncInfo;

// 进程同步结构体共享内存名称
constexpr const char* ProcessSyncShmName = "/ipc_process_sync";

} // namespace Define
} // namespace IpcInterface
