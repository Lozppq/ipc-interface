/**
 * @file TagMessage.h
 * @brief 收发消息结构体（供 StreamShmCreator / SendWork / ReceiveWork 共用）
 */

#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

namespace IpcInterface {
namespace MulProcess {

class StreamShmCreator;

struct TagSendMessage {
    std::vector<uint8_t> data;
    uint16_t message_id{0};
    StreamShmCreator* shm{NULL};
};

struct TagReceiveMessage {
    std::vector<uint8_t> data;
    uint16_t message_id{0};
};

using ReceiveHandler = std::function<void(std::shared_ptr<TagReceiveMessage>)>;

} // namespace MulProcess
} // namespace IpcInterface
