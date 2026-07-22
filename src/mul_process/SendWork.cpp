/**
 * @file SendWork.cpp
 * @brief 消息发送工作线程实现
 */

#include "SendWork.h"

namespace IpcInterface {
namespace MulProcess {

SendWork::SendWork(StreamShmCreator* shm, std::string name)
    : MessageThread(1024, std::move(name)), shm_(shm), name_(std::move(name)) {}

SendWork::~SendWork() {
    shm_ = NULL;
}

void SendWork::send(std::vector<uint8_t> msg, StreamShmCreator* shm, uint32_t to_pid) {
    if (msg.empty()) {
        return;
    }
    auto tag = std::make_shared<TagSendMessage>();
    tag->data = std::move(msg);
    tag->shm = shm == NULL ? shm_ : shm;
    tag->to_pid = to_pid;
    post([this, tag = std::move(tag)]() {
        SendMessage(tag);
    });
}

void SendWork::SendMessage(std::shared_ptr<TagSendMessage> tag) {
    if (!isRunning() || !tag || tag->data.empty() || !tag->shm) {
        return;
    }
    tag->shm->send(tag->data);
}

void SendWork::OnThreadInit() {

}

} // namespace MulProcess
} // namespace IpcInterface
