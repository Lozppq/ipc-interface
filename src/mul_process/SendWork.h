/**
 * @file SendWork.h
 * @brief 消息发送工作线程
 * @details 继承 MessageThread，在独立工作线程中执行共享内存发送。
 */

#pragma once

#include "../model/MessageThread.h"
#include "ShmCreator.h"
#include <cstdint>
#include <memory>
#include <vector>

namespace IpcInterface {
namespace MulProcess {

struct TagSendMessage {
    std::vector<uint8_t> data;
};

class SendWork : public Model::MessageThread {
public:
    explicit SendWork(ShmCreator* shm, std::string name = {});
    ~SendWork();

    SendWork(const SendWork&) = delete;
    SendWork& operator=(const SendWork&) = delete;

    void send(std::shared_ptr<TagSendMessage> msg);
    void send(std::vector<uint8_t> msg);

protected:
    void OnThreadInit() override;
    void SendMessage(std::shared_ptr<TagSendMessage> msg);

private:
    ShmCreator* shm_{nullptr};
    std::string name_;
};

} // namespace MulProcess
} // namespace IpcInterface
