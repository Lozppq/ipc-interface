/**
 * @file SendWork.cpp
 * @brief 消息发送工作线程实现
 */

#include "SendWork.h"
#include "StreamShmCreator.h"

namespace IpcInterface {
namespace MulProcess {

SendWork::SendWork(std::shared_ptr<StreamShmCreator> shm, std::string name)
    : MessageThread(8192, std::move(name)), 
    shm_(std::move(shm)) {
        
    }

SendWork::~SendWork() {
    shm_.reset();
    stop();
}

void SendWork::send(std::vector<uint8_t> msg, uint16_t message_id, std::shared_ptr<StreamShmCreator> shm) {
    if (msg.empty()) {
        return;
    }
    auto tag = std::make_shared<TagSendMessage>();
    tag->data = std::move(msg);
    tag->message_id = message_id;
    tag->shm = shm ? std::move(shm) : shm_;
    post([this, tag = std::move(tag)]() {
        SendMessage(tag);
    });
}

void SendWork::send(std::shared_ptr<TagSendMessage> buf_msg, std::shared_ptr<StreamShmCreator> shm_keep) {
    if (!buf_msg || buf_msg->data.empty()) {
        return;
    }
    if (shm_keep) {
        buf_msg->shm = std::move(shm_keep);
    } else if (!buf_msg->shm) {
        buf_msg->shm = shm_;
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
        postTimer(1, [this, tag]() {
            SendMessage(tag);
        });
    } else {
        LOG_ERROR("SendWork: send failed data size=%u, toShm=%s",
                  tag->data.size(), tag->shm->get_shm_name().c_str());
    }
}

void SendWork::OnThreadInit() {

}

} // namespace MulProcess
} // namespace IpcInterface
