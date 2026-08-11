/**
 * @file SendWork.cpp
 * @brief 消息发送工作线程实现
 */

#include "SendWork.h"
#include "StreamShmCreator.h"

namespace IpcInterface {
namespace MulProcess {

SendWork::SendWork(StreamShmCreator* shm, std::string name)
    : MessageThread(8192), shm_(shm), name_(std::move(name)) {}

SendWork::~SendWork() {
    shm_ = NULL;
}

void SendWork::send(std::vector<uint8_t> msg, uint16_t message_id, std::shared_ptr<StreamShmCreator> shm) {
    if (msg.empty()) {
        return;
    }
    auto tag = std::make_shared<TagSendMessage>();
    tag->data = std::move(msg);
    tag->message_id = message_id;
    post([this, tag = std::move(tag), shm = std::move(shm)]() {
        tag->shm = shm ? shm.get() : shm_;
        SendMessage(tag);
    });
}

void SendWork::send(std::shared_ptr<TagSendMessage> buf_msg, std::shared_ptr<StreamShmCreator> shm_keep) {
    if (!buf_msg || buf_msg->data.empty()) {
        return;
    }
    post([this, buf_msg = std::move(buf_msg), shm_keep = std::move(shm_keep)]() {
        if (shm_keep) {
            buf_msg->shm = shm_keep.get();
        }
        if (!buf_msg->shm) {
            buf_msg->shm = shm_;
        }
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
        postTimer(1, [this, tag]() {
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
