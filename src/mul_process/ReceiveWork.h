/**
 * @file ReceiveWork.h
 * @brief 消息接收工作线程
 * @details 继承 MessageThread，在独立工作线程中轮询接收消息并触发回调。
 */

#ifndef RECEIVE_WORK_H
#define RECEIVE_WORK_H

#include "../model/MessageThread.h"
#include "ShmManager.h"
#include <cstdint>
#include <functional>

class ReceiveWork : public MessageThread {
public:
    static constexpr uint32_t DEFAULT_MESSAGE_SIZE = 1024 * 64;
    using ReceiveHandler = std::function<void(const uint8_t*, uint32_t)>;

    ReceiveWork(ShmManager* shm, ReceiveHandler handler, uint32_t buffer_size = DEFAULT_MESSAGE_SIZE);
    ~ReceiveWork();

    ReceiveWork(const ReceiveWork&) = delete;
    ReceiveWork& operator=(const ReceiveWork&) = delete;

protected:
    void OnThreadInit() override;
    void ReceiveMessage();

private:
    ShmManager* shm_{nullptr};
    ReceiveHandler receive_handler_;
    uint8_t* buf_msg_{nullptr};
    uint32_t buffer_size_{DEFAULT_MESSAGE_SIZE};
};

#endif
