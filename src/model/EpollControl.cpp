/**
 * @file EpollControl.cpp
 * @brief epoll 控制器实现
 */

#include "EpollControl.h"
#include "../log/Log_Print.h"
#if defined(__linux__)
#include <sys/epoll.h>
#endif

namespace IpcInterface {
namespace Model {

EpollControl::EpollControl(size_t queue_size)
    : m_queue(queue_size) {
#if defined(__linux__)
    if (m_event.isValid()) {
        m_epoll.add(m_event.getFd(), EPOLLIN | EPOLLET);
    }
#endif
}

EpollControl::~EpollControl() {
    while (!m_timers.empty()) {
        stopTimer(m_timers.begin()->first);
    }
}

bool EpollControl::post(std::function<void()> callback) {
    if (!callback) {
        return false;
    }
    if (m_queue.push(std::move(callback))) {
        m_event.wake();
        return true;
    }
    LOG_ERROR("EpollControl::post queue full, drop callback");
    return false;
}

int EpollControl::startTimer(uint32_t interval_ms, bool periodic, TimerCallback callback) {
    TimerItem item;
    if (!item.m_timer.start(interval_ms, periodic)) {
        return -1;
    }
    int fd = item.m_timer.getFd();
#if defined(__linux__)
    if (!m_epoll.add(fd, EPOLLIN | EPOLLET)) {
        return -1;
    }
#else
    (void)fd;
    return -1;
#endif
    item.m_callback = std::move(callback);
    m_timers.emplace(fd, std::move(item));
    return fd;
}

void EpollControl::stopTimer(int fd) {
    auto it = m_timers.find(fd);
    if (it == m_timers.end()) {
        return;
    }
    m_epoll.del(fd);
    it->second.m_timer.stop();
    m_timers.erase(it);
}

int EpollControl::wait(int timeout_ms) {
    int n = m_epoll.wait(timeout_ms);
    if (n <= 0) {
        return n;
    }
#if defined(__linux__)
    const epoll_event* evs = m_epoll.events();
    for (int i = 0; i < n; ++i) {
        int fd = evs[i].data.fd;
        if (fd == m_event.getFd()) {
            m_event.read();
            std::function<void()> task;
            while (m_queue.pop(task)) {
                if (task) {
                    task();
                }
            }
            continue;
        }
        auto it = m_timers.find(fd);
        if (it == m_timers.end()) {
            continue;
        }
        it->second.m_timer.read();
        TimerCallback cb = it->second.m_callback;
        const bool periodic = it->second.m_timer.isPeriodic();
        if (cb) {
            cb(fd);
        }
        if (!periodic) {
            stopTimer(fd);
        }
    }
#endif
    return n;
}

void EpollControl::wake() {
    m_event.wake();
}

} // namespace Model
} // namespace IpcInterface
