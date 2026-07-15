/**
 * @file ReceiveWork.cpp
 * @brief 消息接收工作线程实现
 */

#include "ReceiveWork.h"

namespace IpcInterface {
namespace MulProcess {

ReceiveWork::ReceiveWork(ShmCreator* shm, ReceiveHandler handler)
    : MessageThread(1024, "RecvWork"), shm_(shm), receive_handler_(handler) {}

ReceiveWork::~ReceiveWork() {
    receive_handler_ = nullptr;
    shm_ = nullptr;
}

void ReceiveWork::setReceiveHandler(ReceiveHandler handler) {
    receive_handler_ = std::move(handler);
}

void ReceiveWork::ReceiveMessage() {
    if (!isRunning() || !shm_ || !receive_handler_) {
        return;
    }
    uint32_t len = shm_->recv(buf_msg_);
    if (len > 0) {
        auto msg = std::make_shared<TagReceiveMessage>();
        msg->data.swap(buf_msg_);
        receive_handler_(msg);
    }
}

void ReceiveWork::OnThreadInit() {
    startTimer(0, [this]() {
        ReceiveMessage();
    });
}

} // namespace MulProcess
} // namespace IpcInterface