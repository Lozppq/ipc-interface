/**
 * @file MessageService.h
 * @brief 跨进程消息服务
 * @details 单例类，继承 MessageThread，封装进程间消息收发能力。
 */

#pragma once

#include "../model/MessageThread.h"
#include "ReceiveWork.h"
#include "ShmCreator.h"
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace IpcInterface {
namespace MulProcess {

class MessageService : public Model::MessageThread {
public:
    using ReceiveHandler = ReceiveWork::ReceiveHandler;
    static MessageService& getInstance();

    MessageService(const MessageService&) = delete;
    MessageService& operator=(const MessageService&) = delete;

    bool init(const char* shm_name, uint32_t size, bool create);
    bool isInitialized() const;

    int send(const std::vector<uint8_t>& msg);
    void sendAsync(std::vector<uint8_t> msg);
    void receive(std::shared_ptr<TagReceiveMessage> msg);

    void setReceiveHandler(ReceiveHandler handler);
    void startReceive();
    void stopReceive();


protected:
    void OnThreadInit() override;

private:
    MessageService();
    ~MessageService();

private:
    std::unique_ptr<ShmCreator> shm_;
    ReceiveHandler receive_handler_;
    bool initialized_{false};
    bool receiving_{false};
    std::unique_ptr<ReceiveWork> receive_work_;
};

} // namespace MulProcess
} // namespace IpcInterface
