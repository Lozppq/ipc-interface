/**
 * @file MessageThread.h
 * @brief 消息线程类
 * @details 继承 ThreadBase，用 EpollControl（epoll + eventfd + timerfd）做任务投递和定时器。
 */

#pragma once

#include "ThreadBase.h"
#include "EpollControl.h"
#include <functional>
#include <cstdint>

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
     * @return 是否成功
     */
    bool post(std::function<void()> task);

    /**
     * @brief 投递一次性定时器任务
     * @param delay_ms 延迟毫秒数
     * @param callback 定时器回调函数
     */
    void postTimer(uint32_t delay_ms, TimerCallback callback);
    int startTimer(uint32_t interval_ms, bool periodic, TimerCallback callback);
    void stopTimer(int timer_fd);
    void stop() override;

protected:
    void Run() override;

private:
    EpollControl m_epoll;
};

} // namespace Model
} // namespace IpcInterface
