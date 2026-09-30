/**
 * @file TagMessage.h
 * @brief 收发消息结构体（供 StreamShmCreator / ShmManager / ReceiveWork 共用）
 */

#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace IpcInterface
{
namespace MulProcess
{

#ifndef kSendMaxRetry
#define kSendMaxRetry 5
#endif

struct TagSendMessage
{
    std::vector<uint8_t> m_data;
    uint16_t m_message_id{0};
};

struct TagReceiveMessage
{
    std::vector<uint8_t> m_data;
    uint16_t m_message_id{0};
};

// ALLOCATE_SHM 数据部分（m_data，已含 2 字节子ID）：
// [2] u8 逻辑进程id，[3] u8 角色（0 发送者 / 1 接收者），
// [4..7] u32 单槽位大小，[8..11] u32 槽位数量，
// [12] u8 名称长度 n，[13..] n 字节名称
// 接收者回包另在末尾追加 u8 读者下标
struct AllocateShmPayload
{
    uint8_t m_logic_id{0};       // 申请者逻辑槽位
    uint8_t m_role{0};           // ALLOCATE_SHM_SENDER / ALLOCATE_SHM_RECEIVER
    uint32_t m_slot_size{0};     // 单槽位大小
    uint32_t m_slot_count{0};    // 槽位数量
    std::string m_shm_name;      // 共享内存名称
    uint8_t m_reader_index{0xFF}; // 读者下标，0xFF 表示非读者
};

// RELEASE_SHM 数据部分（m_data，已含 2 字节子ID）：
// [2] u8 名称长度 n，[3..] n 字节名称，
// 随后 u8 逻辑进程id，u8 0正常释放 / 1强制释放
struct ReleaseShmPayload
{
    std::string m_shm_name;      // 共享内存名称
    uint8_t m_logic_id{0};       // 申请释放的逻辑槽位
    uint8_t m_force{0};          // RELEASE_SHM_NORMAL / RELEASE_SHM_FORCE
};

using ReceiveHandler = std::function<void(std::shared_ptr<TagReceiveMessage>)>;

} // namespace MulProcess
} // namespace IpcInterface
