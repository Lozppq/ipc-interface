/**
 * @file MessageThread.cpp
 * @brief 消息线程实现
 * @details 实现消息投递、一次性定时器、周期性定时器功能。
 * 事件循环通过信号量等待，无定时器时无限阻塞，有定时器时带超时等待，
 * 所有定时器操作通过任务队列传递到工作线程，避免并发访问风险。
 */

#if defined(__linux__) && !defined(_GNU_SOURCE)
#define _GNU_SOURCE
#endif
#include "MessageThread.h"
#include <vector>
#if defined(__linux__)
#include <semaphore.h>
#include <time.h>
#endif

namespace IpcInterface {
namespace Model {

MessageThread::MessageThread(size_t queue_size, std::string name)
    : ThreadBase(std::move(name)), queue_(queue_size) {
#if defined(__linux__)
    sem_init(&sem_, 0, 0);
#endif
    timer_ids_.store(0, std::memory_order_release);
}

MessageThread::~MessageThread() {
    stop();
#if defined(__linux__)
    sem_destroy(&sem_);
#endif
}

void MessageThread::stop() {
    setRunning(false);
#if defined(__linux__)
    sem_post(&sem_);
#endif
    wait();
}

void MessageThread::OnThreadInit() {}

void MessageThread::post(std::function<void()> task) {
    if (queue_.push(task)) {
#if defined(__linux__)
        sem_post(&sem_);
#endif
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
        sem_post(&sem_);
#endif
    } else {
#if defined(__linux__)
        // 这里需要定义信号量信号量进行同步操作，在工作线程执行完添加定时器这里才返回
        sem_t done;
        sem_init(&done, 0, 0);
        queue_.push([this, timer_id, delay, cb = std::move(callback), &done]() {
            timers_.insert({
                timer_id,
                std::chrono::steady_clock::now() + delay,
                delay,
                std::move(cb),
                false
            });
            sem_post(&done);
        });
        sem_post(&sem_);
        sem_wait(&done);
        sem_destroy(&done);
#else
        queue_.push([this, timer_id, delay, cb = std::move(callback)]() {
            timers_.insert({
                timer_id,
                std::chrono::steady_clock::now() + delay,
                delay,
                std::move(cb),
                false
            });
        });
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
        sem_post(&sem_);
#endif
    } else {
#if defined(__linux__)
        queue_.push([this, timer_id, interval, cb = std::move(callback)]() {
            timers_.insert({
                timer_id,
                std::chrono::steady_clock::now() + interval,
                interval,
                std::move(cb),
                true
            });
        });
        sem_post(&sem_);
#else
        queue_.push([this, timer_id, interval, cb = std::move(callback)]() {
            timers_.insert({
                timer_id,
                std::chrono::steady_clock::now() + interval,
                interval,
                std::move(cb),
                true
            });
        });
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
        queue_.push(std::move(do_stop));
#if defined(__linux__)
        sem_post(&sem_);
#endif
    }
}

void MessageThread::Run() {
    while (isRunning()) {
        auto now = std::chrono::steady_clock::now();
        
        if (timers_.empty()) {
#if defined(__linux__)
            // 无定时器，无限等待消息
            sem_wait(&sem_);
#endif
        } else {
            auto remaining = timers_.begin()->expiry - now;
            if (remaining > std::chrono::steady_clock::duration::zero()) {
#if defined(__linux__)
                const auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(remaining).count();
                timespec ts{};
#if defined(__GLIBC__) && (__GLIBC__ > 2 || (__GLIBC__ == 2 && __GLIBC_MINOR__ >= 30))
                constexpr clockid_t clk = CLOCK_MONOTONIC;
#else
                constexpr clockid_t clk = CLOCK_REALTIME;
#endif
                clock_gettime(clk, &ts);
                ts.tv_sec += ns / 1000000000L;
                ts.tv_nsec += ns % 1000000000L;
                if (ts.tv_nsec >= 1000000000L) {
                    ++ts.tv_sec;
                    ts.tv_nsec -= 1000000000L;
                }
#if defined(__GLIBC__) && (__GLIBC__ > 2 || (__GLIBC__ == 2 && __GLIBC_MINOR__ >= 30))
                sem_clockwait(&sem_, CLOCK_MONOTONIC, &ts);
#else
                sem_timedwait(&sem_, &ts); // 老接口使用系统时间，要多注意校时影响
#endif
#endif
            }
        }

        // 处理队列中的所有任务（包括定时器注册任务）
        std::function<void()> task;
        // 处理已过期的定时器
        now = std::chrono::steady_clock::now();
        while (!queue_.isEmpty() || (!timers_.empty() && timers_.begin()->expiry <= now)) {
            if (queue_.pop(task)) {
                task();
            }

            now = std::chrono::steady_clock::now();
            if (!timers_.empty() && timers_.begin()->expiry <= now) {
                Timer timer = *timers_.begin();
                timers_.erase(timers_.begin());
                // 这里直接执行回调
                timer.callback();
                // 周期定时器重新计算下次超时时间
                if (timer.periodic) {
                    timer.expiry = now + timer.interval; // 重新计算下次超时时间，用当前进来now计算下次相对时间，而不是上一次时间，避免“雪崩”现象
                    timers_.insert(std::move(timer));
                }
            }
        }
    }
}

} // namespace Model
} // namespace IpcInterface
