/**
 * @file PidNameInfo.h
 * @brief 共享内存通道与收发逻辑槽位
 */

#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace IpcInterface
{
namespace MulProcess
{

enum : uint8_t
{
    ALLOCATE_SHM_SENDER = 0,
    ALLOCATE_SHM_RECEIVER = 1,
};

class PidNameInfo
{
public:
    bool hasSender(uint8_t logic_id) const;
    bool hasReceiver(uint8_t logic_id) const;
    void addSender(uint8_t logic_id);
    void addReceiver(uint8_t logic_id);
    void addReceiver(uint8_t logic_id, uint8_t reader_index);
    void removeSender(uint8_t logic_id);
    void removeReceiver(uint8_t logic_id);
    void remove(uint8_t logic_id);
    void addAllocateRole(uint8_t logic_id, uint8_t role, uint8_t reader_index = 0xFF);
    uint8_t readerIndexOf(uint8_t logic_id) const;
    bool empty() const;

    std::string m_shm_name;              // 共享内存名称
    std::vector<uint8_t> m_senders;      // 已挂接的发送者逻辑槽位
    std::vector<uint8_t> m_receivers;    // 已挂接的接收者逻辑槽位
    std::vector<uint8_t> m_receiver_slots; // 与 m_receivers 一一对应的读者下标

private:
    static bool hasLogic(const std::vector<uint8_t>& v, uint8_t id);
    static void addLogic(std::vector<uint8_t>& v, uint8_t id);
    static void removeLogic(std::vector<uint8_t>& v, uint8_t id);
};

} // namespace MulProcess
} // namespace IpcInterface
