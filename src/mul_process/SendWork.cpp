/**
 * @file SendWork.cpp
 * @brief 消息发送工作线程实现
 */

#include "SendWork.h"

namespace IpcInterface {
namespace MulProcess {

SendWork::SendWork(ShmCreator* shm, std::string name)
    : MessageThread(1024, std::move(name)), shm_(shm), name_(std::move(name)) {}

SendWork::~SendWork() {
    shm_ = nullptr;
}

void SendWork::send(std::shared_ptr<TagSendMessage> msg) {
    if (!msg || msg->data.empty()) {
        return;
    }
    post([this, msg = std::move(msg)]() {
        SendMessage(msg);
    });
}

void SendWork::send(std::vector<uint8_t> msg) {
    if (msg.empty()) {
        return;
    }
    auto tag = std::make_shared<TagSendMessage>();
    tag->data = std::move(msg);
    send(std::move(tag));
}

void SendWork::SendMessage(std::shared_ptr<TagSendMessage> msg) {
    if (!isRunning() || !shm_ || !msg || msg->data.empty()) {
        return;
    }
    shm_->send(msg->data);
}

void SendWork::OnThreadInit() {

}

} // namespace MulProcess
} // namespace IpcInterface
