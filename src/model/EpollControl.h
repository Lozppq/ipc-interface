/**
 * @file EpollControl.h
 * @brief epoll 控制器：定时器 fd + eventfd 回调投递
 */

#pragma once

#include "EpollHandle.h"
#include "EventHandle.h"
#include "TimerHandle.h"
#include "LockFreeQueue.h"
#include <cstdint>
#include <functional>
#include <map>

namespace IpcInterface {
namespace Model {

using TimerCallback = std::function<void(int fd)>;

class EpollControl {
public:
    explicit EpollControl(size_t queue_size = 1024);
    ~EpollControl();

    EpollControl(const EpollControl&) = delete;
    EpollControl& operator=(const EpollControl&) = delete;

    bool post(std::function<void()> callback);
    int startTimer(uint32_t interval_ms, bool periodic, TimerCallback callback);
    void stopTimer(int fd);
    int wait(int timeout_ms = -1);
    void wake();

private:
    struct TimerItem {
        TimerHandle m_timer;
        TimerCallback m_callback;
    };

    EpollHandle m_epoll;
    EventHandle m_event;
    LockFreeQueue<std::function<void()>> m_queue;
    std::map<int, TimerItem> m_timers;
};

} // namespace Model
} // namespace IpcInterface
