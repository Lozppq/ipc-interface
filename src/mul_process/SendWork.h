/**
 * @file SendWork.h
 * @brief 消息发送工作线程
 * @details 继承 MessageThread，在独立工作线程中执行共享内存发送。
 */

#pragma once

#include "../model/MessageThread.h"
#include "StreamShmCreator.h"
#include <cstdint>
#include <memory>
#include <vector>

namespace IpcInterface {
namespace MulProcess {

struct TagSendMessage {
    std::vector<uint8_t> data;
    StreamShmCreator* shm;
    uint32_t to_pid;
};

class SendWork : public Model::MessageThread {
public:
    explicit SendWork(StreamShmCreator* shm = NULL, std::string name = {});
    ~SendWork();

    SendWork(const SendWork&) = delete;
    SendWork& operator=(const SendWork&) = delete;

    // 这里默认使用初始化的共享内存发送，如果初始化未设置共享内存则使用传入的共享内存发送,如果传入的共享内存仍为NULL则不发送
    void send(std::vector<uint8_t> msg, StreamShmCreator* shm = NULL, uint32_t to_pid = 0);

protected:
    void OnThreadInit() override;
    void SendMessage(std::shared_ptr<TagSendMessage> tag);

private:
    StreamShmCreator* shm_{NULL};
    std::string name_;
};

} // namespace MulProcess
} // namespace IpcInterface
