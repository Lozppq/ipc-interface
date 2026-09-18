/**
 * @file TagMessage.h
 * @brief 收发消息结构体（供 StreamShmCreator / ShmManager / ReceiveWork 共用）
 */

#pragma once

#include <cstdint>
#include <functional>
#include <memory>
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

using ReceiveHandler = std::function<void(std::shared_ptr<TagReceiveMessage>)>;

} // namespace MulProcess
} // namespace IpcInterface
