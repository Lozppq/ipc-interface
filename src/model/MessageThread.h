/**
 * @file MessageThread.h
 * @brief 消息线程类
 * @details 继承自 ThreadBase，实现支持消息投递和定时器功能的线程模型。
 * 使用无锁队列存储任务，eventfd+poll 阻塞唤醒，优先队列管理定时器，
 * 所有任务（消息、定时器回调）统一在工作线程中执行，保证线程安全。
 */

#pragma once

#include "ThreadBase.h"
#include "EventHandle.h"
#include "LockFreeQueue.h"
#include <functional>
#include <chrono>
#include <set>
#include <thread>
#include <atomic>
#include <vector>

namespace IpcInterface {
namespace Model {
class MessageThread : public ThreadBase {
public:
    /**
     * @brief 构造函数
     * @param queue_size 任务队列容量，默认1024
     * @param name 工作线程名
     */
    explicit MessageThread(size_t queue_size = 1024, std::string name = {});

    /**
     * @brief 析构函数，停止线程
     */
    ~MessageThread();

    /**
     * @brief 投递普通任务
     * @param task 任务回调函数
     */
    void post(std::function<void()> task);

    /**
     * @brief 投递一次性定时器任务
     * @param delay_ms 延迟毫秒数
     * @param callback 定时器回调函数
     */
    void postTimer(int delay_ms, std::function<void()> callback);

    /**
     * @brief 启动周期性定时器
     * @param interval_ms 周期毫秒数
     * @param callback 定时器回调函数
     * @param periodic 是否周期性
     * @return 定时器id
     */
    uint32_t startTimer(int interval_ms, std::function<void()> callback, bool periodic = true);

    /**
     * @brief 停止周期性定时器
     * @param timer_id 定时器id
     */
    void stopTimer(uint32_t timer_id);

    /**
     * @brief 停止线程
     */
    void stop() override;

protected:
    void Run() override;

private:
    /**
     * @brief 定时器结构，存储超时时间、周期和回调
     */
    struct Timer {
        uint32_t m_id;                                   // 定时器id
        std::chrono::steady_clock::time_point m_expiry; // 超时时间点
        std::chrono::milliseconds m_interval;            // 周期（一次性为0）
        std::function<void()> m_callback;                // 回调函数
        bool m_periodic;                                 // 是否周期性
        
        // 优先队列比较函数，expiry 小的优先级高
        bool operator<(const Timer& other) const {
            if (m_expiry != other.m_expiry) return m_expiry < other.m_expiry;
            return m_id < other.m_id;
        }
    };

    LockFreeQueue<std::function<void()>> m_queue;
    EventHandle m_event; // eventfd：跨线程唤醒与 poll 等待
    std::set<Timer> m_timers; // 定时器堆
    std::atomic<uint32_t> m_timer_ids;  // 定时器id，自增id，用于区分不同的定时器
    // 回收的定时器id，用于避免重复添加定时器
    std::vector<uint32_t> m_recycled_timer_ids;
};

} // namespace Model
} // namespace IpcInterface
