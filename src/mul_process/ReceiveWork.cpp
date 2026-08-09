/**
 * @file ReceiveWork.cpp
 * @brief 消息接收工作线程实现
 */

#include "ReceiveWork.h"
#include "StreamShmCreator.h"

namespace IpcInterface {
namespace MulProcess {

ReceiveWork::ReceiveWork(StreamShmCreator* shm, ReceiveHandler handler, std::string name)
    : MessageThread(1024, std::move(name)), 
    shm_(shm), 
    receive_handler_(handler), 
    name_(std::move(name)) {}

ReceiveWork::~ReceiveWork() {
    receive_handler_ = NULL;
    shm_ = NULL;
}

void ReceiveWork::setReceiveHandler(ReceiveHandler handler) {
    receive_handler_ = std::move(handler);
}

void ReceiveWork::ReceiveMessage() {
    if (!isRunning() || !shm_ || !receive_handler_) {
        return;
    }
    std::shared_ptr<TagReceiveMessage> buf_msg_ = std::make_shared<TagReceiveMessage>();
    if (shm_->recv(buf_msg_) > 0) {
        receive_handler_(buf_msg_);
    } else {
        LOG_ERROR("ReceiveWork: ReceiveMessage failed, shm_name = %s", shm_->get_shm_name().c_str());
    }
}

void ReceiveWork::OnThreadInit() {
    startTimer(0, [this]() {
        ReceiveMessage();
    });
}

} // namespace MulProcess
} // namespace IpcInterface