/**
 * @file MessageThread.h
 * @brief 消息线程类
 * @details 继承自 ThreadBase，实现支持消息投递和定时器功能的线程模型。
 * 使用无锁队列存储任务，信号量实现阻塞唤醒，优先队列管理定时器，
 * 所有任务（消息、定时器回调）统一在工作线程中执行，保证线程安全。
 */

#pragma once

#include "ThreadBase.h"
#include "LockFreeQueue.h"
#include <functional>
#include <chrono>
#include <set>
#include <thread>
#if defined(__linux__)
#include <semaphore.h>
#endif

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
     * @brief 析构函数，停止线程并销毁信号量
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
     * @return 定时器id
     */
    uint32_t postTimer(int delay_ms, std::function<void()> callback);
    
    /**
     * @brief 启动周期性定时器
     * @param interval_ms 周期毫秒数
     * @param callback 定时器回调函数
     * @return 定时器id
     */
    uint32_t startTimer(int interval_ms, std::function<void()> callback);

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
    void OnThreadInit() override;
    void Run() override;

private:
    /**
     * @brief 定时器结构，存储超时时间、周期和回调
     */
    struct Timer {
        uint32_t id;                                   // 定时器id
        std::chrono::steady_clock::time_point expiry; // 超时时间点
        std::chrono::milliseconds interval;            // 周期（一次性为0）
        std::function<void()> callback;                // 回调函数
        bool periodic;                                 // 是否周期性
        
        // 优先队列比较函数，expiry 小的优先级高
        bool operator<(const Timer& other) const {
            if (expiry != other.expiry) return expiry < other.expiry;
            return id < other.id;
        }
    };
private:
    
    LockFreeQueue<std::function<void()>> queue_;  // 无锁任务队列
#if defined(__linux__)
    sem_t sem_;                                   // 唤醒信号量
#endif
    std::set<Timer> timers_; // 定时器堆
    std::atomic<uint32_t> timer_ids_;  // 定时器id，自增id，用于区分不同的定时器
};

} // namespace Model
} // namespace IpcInterface
