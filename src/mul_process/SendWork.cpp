/**
 * @file SendWork.cpp
 * @brief 消息发送工作线程实现
 */

#include "SendWork.h"
#include "StreamShmCreator.h"

namespace IpcInterface {
namespace MulProcess {

SendWork::SendWork(StreamShmCreator* shm, std::string name)
    : MessageThread(1024, std::move(name)), shm_(shm), name_(std::move(name)) {}

SendWork::~SendWork() {
    shm_ = NULL;
}

void SendWork::send(std::vector<uint8_t> msg, uint16_t message_id, StreamShmCreator* shm) {
    if (msg.empty()) {
        return;
    }
    auto tag = std::make_shared<TagSendMessage>();
    tag->data = std::move(msg);
    tag->message_id = message_id;
    tag->shm = shm == NULL ? shm_ : shm;
    post([this, tag = std::move(tag)]() {
        SendMessage(tag);
    });
}

void SendWork::send(std::shared_ptr<TagSendMessage> buf_msg) {
    if (!buf_msg || buf_msg->data.empty() || !buf_msg->shm) {
        return;
    }
    post([this, buf_msg = std::move(buf_msg)]() {
        SendMessage(buf_msg);
    });
}

void SendWork::SendMessage(const std::shared_ptr<TagSendMessage>& tag) {
    if (!isRunning() || !tag || tag->data.empty() || !tag->shm) {
        return;
    }
    if (tag->shm->send(tag) >= 0) {
        return;
    }
    tag->retry_count++;
    if (!isRunning()) {
        return;
    }
    if (tag->retry_count <= kSendMaxRetry) {
        post([this, tag]() {
            SendMessage(tag);
        });
    } else {
        LOG_ERROR("SendWork: send failed after %u retries, message_id=%u",
                  tag->retry_count, static_cast<unsigned>(tag->message_id));
    }
}

void SendWork::OnThreadInit() {

}

} // namespace MulProcess
} // namespace IpcInterface
