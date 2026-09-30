/**
 * @file PidNameInfo.cpp
 * @brief 共享内存通道与收发逻辑槽位
 */

#include "PidNameInfo.h"
#include "../define/Common.h"
#include <algorithm>
#include <cstddef>

namespace IpcInterface
{
namespace MulProcess
{

bool PidNameInfo::hasSender(uint8_t logic_id) const
{
    return hasLogic(m_senders, logic_id);
}

bool PidNameInfo::hasReceiver(uint8_t logic_id) const
{
    return hasLogic(m_receivers, logic_id);
}

void PidNameInfo::addSender(uint8_t logic_id)
{
    addLogic(m_senders, logic_id);
}

void PidNameInfo::addReceiver(uint8_t logic_id)
{
    addReceiver(logic_id, 0xFF);
}

void PidNameInfo::addReceiver(uint8_t logic_id, uint8_t reader_index)
{
    if (logic_id >= Define::INVALID_FD || hasReceiver(logic_id))
        return;
    m_receivers.push_back(logic_id);
    m_receiver_slots.push_back(reader_index);
}

void PidNameInfo::removeSender(uint8_t logic_id)
{
    removeLogic(m_senders, logic_id);
}

void PidNameInfo::removeReceiver(uint8_t logic_id)
{
    for (size_t i = 0; i < m_receivers.size(); i++)
    {
        if (m_receivers[i] != logic_id)
            continue;
        m_receivers.erase(m_receivers.begin() + static_cast<std::ptrdiff_t>(i));
        if (i < m_receiver_slots.size())
            m_receiver_slots.erase(m_receiver_slots.begin() + static_cast<std::ptrdiff_t>(i));
        return;
    }
}

void PidNameInfo::remove(uint8_t logic_id)
{
    removeSender(logic_id);
    removeReceiver(logic_id);
}

void PidNameInfo::addAllocateRole(uint8_t logic_id, uint8_t role, uint8_t reader_index)
{
    if (role == ALLOCATE_SHM_RECEIVER)
        addReceiver(logic_id, reader_index);
    else
        addSender(logic_id);
}

uint8_t PidNameInfo::readerIndexOf(uint8_t logic_id) const
{
    for (size_t i = 0; i < m_receivers.size(); i++)
    {
        if (m_receivers[i] == logic_id)
            return i < m_receiver_slots.size() ? m_receiver_slots[i] : 0xFF;
    }
    return 0xFF;
}

bool PidNameInfo::empty() const
{
    return m_senders.empty() && m_receivers.empty();
}

bool PidNameInfo::hasLogic(const std::vector<uint8_t>& v, uint8_t id)
{
    return std::find(v.begin(), v.end(), id) != v.end();
}

void PidNameInfo::addLogic(std::vector<uint8_t>& v, uint8_t id)
{
    if (id >= Define::INVALID_FD || hasLogic(v, id))
        return;
    v.push_back(id);
}

void PidNameInfo::removeLogic(std::vector<uint8_t>& v, uint8_t id)
{
    v.erase(std::remove(v.begin(), v.end(), id), v.end());
}

} // namespace MulProcess
} // namespace IpcInterface
