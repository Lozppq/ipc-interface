/**
 * @file SendWork.h
 * @brief 消息发送工作线程
 * @details 继承 MessageThread，在独立工作线程中执行共享内存发送。
 */

#pragma once

#include "../model/MessageThread.h"
#include "StreamShmCreator.h"
#include "TagMessage.h"
#include <cstdint>
#include <memory>
#include <vector>

namespace IpcInterface {
namespace MulProcess {


class SendWork : public Model::MessageThread {
public:
    explicit SendWork(StreamShmCreator* shm = NULL, std::string name = {});
    ~SendWork();

    SendWork(const SendWork&) = delete;
    SendWork& operator=(const SendWork&) = delete;

    // 默认使用初始化的共享内存发送；若传入 shm 非空则使用传入的（shared_ptr 保证发送完成前对象存活）
    void send(std::vector<uint8_t> msg, uint16_t message_id, std::shared_ptr<StreamShmCreator> shm = nullptr);
    void send(std::shared_ptr<TagSendMessage> buf_msg, std::shared_ptr<StreamShmCreator> shm_keep = nullptr);

protected:
    void OnThreadInit() override;
    void SendMessage(const std::shared_ptr<TagSendMessage>& tag);

private:
    StreamShmCreator* shm_{NULL};
    std::string name_;
};

} // namespace MulProcess
} // namespace IpcInterface
