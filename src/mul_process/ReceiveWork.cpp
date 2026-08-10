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
    stop();
    receive_handler_ = NULL;
    shm_ = NULL;
}

void ReceiveWork::setReceiveHandler(ReceiveHandler handler) {
    receive_handler_ = std::move(handler);
}

void ReceiveWork::stop() {
    setRunning(false);
    if (shm_) {
        shm_->wakeup_recv();
    }
    MessageThread::stop();
}

void ReceiveWork::ReceiveMessage() {
    if (!isRunning() || !shm_ || !receive_handler_) {
        return;
    }
    auto buf_msg = std::make_shared<TagReceiveMessage>();
    uint32_t n = shm_->recv(buf_msg);
    if (!isRunning()) {
        return;
    }
    if (n > 0) {
        receive_handler_(buf_msg);
        post([this]() { ReceiveMessage(); });
        return;
    }
    // shm 未就绪 / BIT1 关闭 / 超时丢弃：退避再试，禁止 0 周期空转刷屏
    postTimer(1000, [this]() { ReceiveMessage(); });
}

void ReceiveWork::OnThreadInit() {
    post([this]() { ReceiveMessage(); });
}

} // namespace MulProcess
} // namespace IpcInterface
