/**
 * @file MessageThread.cpp
 * @brief 消息线程实现：post / 定时器交给 EpollControl，Run 里 epoll_wait
 */

#include "MessageThread.h"
#include "EventHandle.h"
#include "../log/Log_Print.h"

namespace IpcInterface {
namespace Model {

MessageThread::MessageThread(size_t queue_size, std::string name)
    : ThreadBase(std::move(name)), m_epoll(queue_size) {
}

MessageThread::~MessageThread() {
    stop();
}

void MessageThread::stop() {
    setRunning(false);
    m_epoll.wake();
    wait();
}

bool MessageThread::post(std::function<void()> task) {
    return m_epoll.post(std::move(task));
}

void MessageThread::postTimer(uint32_t delay_ms, TimerCallback callback) {
    startTimer(delay_ms, false, std::move(callback));
}

int MessageThread::startTimer(uint32_t interval_ms, bool periodic, TimerCallback callback) {
    if (interval_ms == 0) {
        return -1;
    }
    if (isInWorkerThread()) {
        return m_epoll.startTimer(interval_ms, periodic, std::move(callback));
    }
    int fd = -1;
    EventHandle done;
    if (!m_epoll.post([this, &done, &fd, interval_ms, periodic, cb = std::move(callback)]() mutable {
        fd = m_epoll.startTimer(interval_ms, periodic, std::move(cb));
        done.wake();
    })) {
        LOG_ERROR("MessageThread::startTimer queue full, drop timer");
        return -1;
    }
    done.wait(-1);
    return fd;
}

void MessageThread::stopTimer(int timer_fd) {
    if (timer_fd < 0) {
        return;
    }
    if (isInWorkerThread()) {
        m_epoll.stopTimer(timer_fd);
        return;
    }
    if (!m_epoll.post([this, timer_fd]() { m_epoll.stopTimer(timer_fd); })) {
        LOG_ERROR("MessageThread::stopTimer queue full, drop stop fd=%d", timer_fd);
    }
}

void MessageThread::Run() {
    while (isRunning()) {
        m_epoll.wait(-1);
    }
}

} // namespace Model
} // namespace IpcInterface
