/**
 * @file MessageThread.cpp
 * @brief 消息线程实现
 * @details 实现消息投递、一次性定时器、周期性定时器功能。
 * 事件循环通过 eventfd+poll 等待，无定时器时无限阻塞，有定时器时带超时等待，
 * 所有定时器操作通过任务队列传递到工作线程，避免并发访问风险。
 */

#include "MessageThread.h"
#include "../log/Log_Print.h"
#if defined(__linux__)
#include <sys/eventfd.h>
#include <poll.h>
#include <unistd.h>
#include <cstdint>
#endif

namespace IpcInterface {
namespace Model {


void MessageThread::wake(int efd) {
#if defined(__linux__)
    uint64_t one = 1;
    ssize_t n = write(efd, &one, sizeof(one));
    (void)n;
#else
    (void)efd;
#endif
}

void MessageThread::drain(int efd) {
#if defined(__linux__)
    uint64_t cnt;
    ssize_t n = read(efd, &cnt, sizeof(cnt));
    (void)n;
#else
    (void)efd;
#endif
}


MessageThread::MessageThread(size_t queue_size, std::string name)
    : ThreadBase(std::move(name)), queue_(queue_size) {
#if defined(__linux__)
    efd_ = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
#endif
    timer_ids_.store(0, std::memory_order_release);
}

MessageThread::~MessageThread() {
    stop();
#if defined(__linux__)
    if (efd_ >= 0) close(efd_);
#endif
}

void MessageThread::stop() {
    setRunning(false);
#if defined(__linux__)
    wake(efd_);
#endif
    wait();
}

void MessageThread::OnThreadInit() {}

void MessageThread::post(std::function<void()> task) {
    if (queue_.push(task)) {
#if defined(__linux__)
        wake(efd_);
#endif
    } else {
        LOG_ERROR("MessageThread::post queue full, drop task");
    }
}

uint32_t MessageThread::postTimer(int delay_ms, std::function<void()> callback) {
    auto timer_id = timer_ids_.fetch_add(1, std::memory_order_relaxed);
    auto delay = std::chrono::milliseconds(delay_ms);
    if (isInWorkerThread()) {
        timers_.insert({
            timer_id,
            std::chrono::steady_clock::now() + delay,
            delay,
            std::move(callback),
            false
        });
#if defined(__linux__)
        wake(efd_);
#endif
    } else {
#if defined(__linux__)
        if (!queue_.push([this, timer_id, delay, cb = std::move(callback)]() {
            timers_.insert({
                timer_id,
                std::chrono::steady_clock::now() + delay,
                delay,
                std::move(cb),
                false
            });
        })) {
            LOG_ERROR("MessageThread::postTimer queue full, drop timer id=%u", timer_id);
        } else {
            wake(efd_);
        }
#else
        if (!queue_.push([this, timer_id, delay, cb = std::move(callback)]() {
            timers_.insert({
                timer_id,
                std::chrono::steady_clock::now() + delay,
                delay,
                std::move(cb),
                false
            });
        })) {
            LOG_ERROR("MessageThread::postTimer queue full, drop timer id=%u", timer_id);
        }
#endif
    }
    return timer_id;
}

uint32_t MessageThread::startTimer(int interval_ms, std::function<void()> callback) {
    auto timer_id = timer_ids_.fetch_add(1, std::memory_order_relaxed);
    auto interval = std::chrono::milliseconds(interval_ms);
    if (isInWorkerThread()) {
        timers_.insert({
            timer_id,
            std::chrono::steady_clock::now() + interval,
            interval,
            std::move(callback),
            true
        });
#if defined(__linux__)
        wake(efd_);
#endif
    } else {
        if (!queue_.push([this, timer_id, interval, cb = std::move(callback)]() {
            timers_.insert({
                timer_id,
                std::chrono::steady_clock::now() + interval,
                interval,
                std::move(cb),
                true
            });
        })) {
            LOG_ERROR("MessageThread::startTimer queue full, drop timer id=%u", timer_id);
        }
#if defined(__linux__)
        else {
            wake(efd_);
        }
#endif
    }
    return timer_id;
}

void MessageThread::stopTimer(uint32_t timer_id) {
    auto do_stop = [this, timer_id]() {
        for (auto it = timers_.begin(); it != timers_.end(); ++it) {
            if (it->id == timer_id) {
                timers_.erase(it);
                break;
            }
        }
    };
    if (isInWorkerThread()) {
        do_stop();
    } else {
        if (!queue_.push(std::move(do_stop))) {
            LOG_ERROR("MessageThread::stopTimer queue full, drop stop id=%u", timer_id);
        }
#if defined(__linux__)
        else {
            wake(efd_);
        }
#endif
    }
}

void MessageThread::Run() {
    while (isRunning()) {
        auto now = std::chrono::steady_clock::now();
#if defined(__linux__)
        int timeout_ms = -1;
        if (!timers_.empty()) {
            auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                timers_.begin()->expiry - now).count();
            timeout_ms = ms > 0 ? static_cast<int>(ms) : 0;
        }
        pollfd pfd{efd_, POLLIN, 0};
        poll(&pfd, 1, timeout_ms);
        if (pfd.revents & POLLIN) drain(efd_);
#endif

        std::function<void()> task;
        now = std::chrono::steady_clock::now();
        while (!queue_.isEmpty() || (!timers_.empty() && timers_.begin()->expiry <= now)) {
            if (queue_.pop(task)) {
                task();
            }

            now = std::chrono::steady_clock::now();
            if (!timers_.empty() && timers_.begin()->expiry <= now) {
                Timer timer = *timers_.begin();
                timers_.erase(timers_.begin());
                timer.callback();
                if (timer.periodic && isRunning()) {
                    timer.expiry = now + timer.interval;
                    timers_.insert(std::move(timer));
                }
            }
        }
    }
}

} // namespace Model
} // namespace IpcInterface
