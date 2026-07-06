/**
 * @file MessageThread.cpp
 * @brief 消息线程实现
 * @details 实现消息投递、一次性定时器、周期性定时器功能。
 * 事件循环通过信号量等待，无定时器时无限阻塞，有定时器时带超时等待，
 * 所有定时器操作通过任务队列传递到工作线程，避免并发访问风险。
 */

#include "MessageThread.h"
#include <semaphore.h>
#include <vector>

MessageThread::MessageThread(size_t queue_size) : queue_(queue_size) {
    sem_init(&sem_, 0, 0);
}

MessageThread::~MessageThread() {
    stop();
    sem_destroy(&sem_);
}

void MessageThread::OnThreadInit() {}

void MessageThread::post(std::function<void()> task) {
    if (queue_.push(task)) {
        sem_post(&sem_);
    }
}

void MessageThread::postTimer(int delay_ms, std::function<void()> callback) {
    auto delay = std::chrono::milliseconds(delay_ms);
    if (isInWorkerThread()) {
        timer_heap_.push({
            std::chrono::steady_clock::now() + delay,
            delay,
            std::move(callback),
            false
        });
    } else {
        queue_.push([this, delay, cb = std::move(callback)]() {
            timer_heap_.push({
                std::chrono::steady_clock::now() + delay,
                delay,
                std::move(cb),
                false
            });
        });
    }
    sem_post(&sem_);
}

void MessageThread::startTimer(int interval_ms, std::function<void()> callback) {
    auto interval = std::chrono::milliseconds(interval_ms);
    if (isInWorkerThread()) {
        timer_heap_.push({
            std::chrono::steady_clock::now() + interval,
            interval,
            std::move(callback),
            true
        });
    } else {
        queue_.push([this, interval, cb = std::move(callback)]() {
            timer_heap_.push({
                std::chrono::steady_clock::now() + interval,
                interval,
                std::move(cb),
                true
            });
        });
    }
    sem_post(&sem_);
}

void MessageThread::Run() {
    while (isRunning()) {
        auto now = std::chrono::steady_clock::now();
        
        if (timer_heap_.empty()) {
            // 无定时器，无限等待消息
            sem_wait(&sem_);
        } else {
            // 有定时器，计算剩余时间，带超时等待
            auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
                timer_heap_.top().expiry - now);
            
            timespec ts;
            ts.tv_sec = remaining.count() / 1000;
            ts.tv_nsec = (remaining.count() % 1000) * 1000000;
            
            sem_timedwait(&sem_, &ts);
        }

        // 处理队列中的所有任务（包括定时器注册任务）
        std::function<void()> task;
        while (queue_.pop(task)) {
            task();
        }

        // 处理已过期的定时器
        now = std::chrono::steady_clock::now();
        std::vector<Timer> expired_timers;
        while (!timer_heap_.empty() && timer_heap_.top().expiry <= now) {
            auto timer = timer_heap_.top();
            timer_heap_.pop();
            // 这里直接执行回调
            timer.callback();
            // 周期定时器重新计算下次超时时间
            if (timer.periodic) {
                timer.expiry = now + timer.interval;
                expired_timers.push_back(timer);
            }
            now = std::chrono::steady_clock::now();
        }
        for (auto& timer : expired_timers) {
            timer_heap_.push(timer);
        }
    }
}