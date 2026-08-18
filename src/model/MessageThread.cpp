/**
 * @file MessageThread.cpp
 * @brief 消息线程实现
 * @details 实现消息投递、一次性定时器、周期性定时器功能。
 * 事件循环通过 EventHandle（eventfd+poll）等待，无定时器时无限阻塞，有定时器时带超时等待，
 * 所有定时器操作通过任务队列传递到工作线程，避免并发访问风险。
 */

#include "MessageThread.h"
#include "../log/Log_Print.h"

namespace IpcInterface {
namespace Model {

MessageThread::MessageThread(size_t queue_size, std::string name)
    : ThreadBase(std::move(name)), m_queue(queue_size) {
    m_timer_ids.store(1, std::memory_order_release);
}

MessageThread::~MessageThread() {
    stop();
}

void MessageThread::stop() {
    setRunning(false);
    m_event.wake();
    wait();
    m_recycled_timer_ids.clear();
    m_timers.clear();
    m_timer_ids.store(1, std::memory_order_release);
    m_event.close();
    m_queue.clear();
}

void MessageThread::post(std::function<void()> task) {
    if (m_queue.push(task)) {
        m_event.wake();
    } else {
        LOG_ERROR("MessageThread::post queue full, drop task");
    }
}

void MessageThread::postTimer(int delay_ms, std::function<void()> callback) {
    auto delay = std::chrono::milliseconds(delay_ms);
    if (isInWorkerThread()) {
        m_timers.insert({
            0,
            std::chrono::steady_clock::now() + delay,
            delay,
            std::move(callback),
            false
        });
        m_event.wake();
    } else {
        if (!m_queue.push([this, delay, cb = std::move(callback)]() {
            m_timers.insert({
                0,
                std::chrono::steady_clock::now() + delay,
                delay,
                std::move(cb),
                false
            });
        })) {
            LOG_ERROR("MessageThread::postTimer queue full, drop timer");
        } else {
            m_event.wake();
        }
    }
}

uint32_t MessageThread::startTimer(int interval_ms, std::function<void()> callback, bool periodic) {
    uint32_t timer_id = 0;
    auto interval = std::chrono::milliseconds(interval_ms);
    if (isInWorkerThread()) {
        if (m_recycled_timer_ids.empty()) {
            timer_id = m_timer_ids.fetch_add(1, std::memory_order_relaxed);
        } else {
            timer_id = m_recycled_timer_ids.back();
            m_recycled_timer_ids.pop_back();
        }
        m_timers.insert({
            timer_id,
            std::chrono::steady_clock::now() + interval,
            interval,
            std::move(callback),
            periodic
        });
        m_event.wake();
    } else {
        // 如果不在工作线程，则将定时器添加到任务队列中，等待返回timer_id
        EventHandle t_event;
        if (!m_queue.push([this, &t_event, &timer_id, interval, periodic, cb = std::move(callback)]() {
            if (m_recycled_timer_ids.empty()) {
                timer_id = m_timer_ids.fetch_add(1, std::memory_order_relaxed);
            } else {
                timer_id = m_recycled_timer_ids.back();
                m_recycled_timer_ids.pop_back();
            }
            m_timers.insert({
                timer_id,
                std::chrono::steady_clock::now() + interval,
                interval,
                std::move(cb),
                periodic
            });
            t_event.wake();
        })) {
            LOG_ERROR("MessageThread::startTimer queue full, drop timer id=%u", timer_id);
        } else {
            m_event.wake();
            t_event.wait(-1); // 阻塞等待timer_id返回
        }
    }
    return timer_id;
}

void MessageThread::stopTimer(uint32_t timer_id) {
    if (isInWorkerThread()) {
        for (auto it = m_timers.begin(); it != m_timers.end(); ++it) {
            if (it->m_id == timer_id) {
                m_recycled_timer_ids.push_back(timer_id);
                m_timers.erase(it);
                break;
            }
        }
    } else {
        if (!m_queue.push([this, timer_id]() {
            for (auto it = m_timers.begin(); it != m_timers.end(); ++it) {
                if (it->m_id == timer_id) {
                    m_recycled_timer_ids.push_back(timer_id);
                    m_timers.erase(it);
                    break;
                }
            }
        })) {
            LOG_ERROR("MessageThread::stopTimer queue full, drop stop id=%u", timer_id);
        } else {
            m_event.wake();
        }
    }
}

void MessageThread::Run() {
    while (isRunning()) {
        auto now = std::chrono::steady_clock::now();
        int timeout_ms = -1;
        if (!m_timers.empty()) {
            auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                m_timers.begin()->m_expiry - now).count();
            timeout_ms = ms > 0 ? static_cast<int>(ms) : 0;
        }
        m_event.wait(timeout_ms);

        std::function<void()> task;
        now = std::chrono::steady_clock::now();
        while (!m_queue.isEmpty() || (!m_timers.empty() && m_timers.begin()->m_expiry <= now)) {
            while (m_queue.pop(task)) { // 先清空任务队列再处理定时器，避免外部投递定时器时，定时器已经被stop
                task();
            }

            now = std::chrono::steady_clock::now();
            if (!m_timers.empty() && m_timers.begin()->m_expiry <= now) {
                Timer timer = *m_timers.begin();
                m_timers.erase(m_timers.begin());
                timer.m_callback();
                if (timer.m_periodic && isRunning()) {
                    timer.m_expiry = now + timer.m_interval;
                    m_timers.insert(std::move(timer));
                }
                now = std::chrono::steady_clock::now();
            }
        }
    }
}

} // namespace Model
} // namespace IpcInterface
