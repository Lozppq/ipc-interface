/**
 * @file ReceiveWork.cpp
 * @brief 消息接收工作线程实现
 */

#include "ReceiveWork.h"
#include "StreamShmCreator.h"

namespace IpcInterface {
namespace MulProcess {

ReceiveWork::ReceiveWork(std::shared_ptr<StreamShmCreator> shm, ReceiveHandler handler, std::string name)
    : MessageThread(1024, std::move(name)),
      m_shm(std::move(shm)),
      m_receive_handler(std::move(handler)) {

}

ReceiveWork::~ReceiveWork() {
    stop();
    m_receive_handler = nullptr;
    m_shm.reset();
}

void ReceiveWork::setReceiveHandler(ReceiveHandler handler) {
    m_receive_handler = std::move(handler);
}

void ReceiveWork::stop() {
    setRunning(false);
    if (m_shm) {
        m_shm->wakeup_recv();
    }
    MessageThread::stop();
}

void ReceiveWork::ReceiveMessage() {
    while (isRunning() && m_shm && m_receive_handler) {
        auto buf_msg = std::make_shared<TagReceiveMessage>();
        const uint32_t n = m_shm->recv(buf_msg);
        if (!isRunning()) {
            break;
        }
        if (n > 0) {
            m_receive_handler(buf_msg);
        }
    }
}

void ReceiveWork::OnThreadInit() {
    post([this]() { ReceiveMessage(); });
}

} // namespace MulProcess
} // namespace IpcInterface
