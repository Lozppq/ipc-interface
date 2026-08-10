/**
 * @file ReceiveWork.h
 * @brief 消息接收工作线程
 * @details 继承 MessageThread，在独立工作线程中轮询接收消息并触发回调。
 */

#pragma once

#include "../model/MessageThread.h"
#include "StreamShmCreator.h"
#include "TagMessage.h"
#include <cstdint>
#include <functional>
#include <memory>
#include <string>

namespace IpcInterface {
namespace MulProcess {

class ReceiveWork : public Model::MessageThread {
public:
    ReceiveWork(StreamShmCreator* shm, ReceiveHandler handler, std::string name = {});
    ~ReceiveWork();
    void setReceiveHandler(ReceiveHandler handler);
    void stop() override;

    ReceiveWork(const ReceiveWork&) = delete;
    ReceiveWork& operator=(const ReceiveWork&) = delete;

protected:
    void OnThreadInit() override;
    void ReceiveMessage();

private:
    StreamShmCreator* shm_{NULL};
    ReceiveHandler receive_handler_;
    std::string name_;
};

} // namespace MulProcess
} // namespace IpcInterface
