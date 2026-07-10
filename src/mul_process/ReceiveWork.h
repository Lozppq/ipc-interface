/**
 * @file ReceiveWork.h
 * @brief 消息接收工作线程
 * @details 继承 MessageThread，在独立工作线程中轮询接收消息并触发回调。
 */

#pragma once

#include "../model/MessageThread.h"
#include "ShmManager.h"
#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

namespace IpcInterface {
namespace MulProcess {
struct TagReceiveMessage {
    std::vector<uint8_t> data;
};

class ReceiveWork : public MessageThread {
public:
    using ReceiveHandler = std::function<void(std::shared_ptr<TagReceiveMessage>)>;

    ReceiveWork(ShmManager* shm, ReceiveHandler handler);
    ~ReceiveWork();
    void setReceiveHandler(ReceiveHandler handler);

    ReceiveWork(const ReceiveWork&) = delete;
    ReceiveWork& operator=(const ReceiveWork&) = delete;

protected:
    void OnThreadInit() override;
    void ReceiveMessage();

private:
    ShmManager* shm_{nullptr};
    ReceiveHandler receive_handler_;
    std::vector<uint8_t> buf_msg_;
};

} // namespace MulProcess
} // namespace IpcInterface
