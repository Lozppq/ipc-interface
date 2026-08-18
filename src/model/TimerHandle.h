/**
 * @file TimerHandle.h
 * @brief timerfd 封装：创建、启动、停止、读取到期次数
 */

#pragma once

#include <cstdint>

namespace IpcInterface {
namespace Model {

class TimerHandle {
public:
    TimerHandle();
    ~TimerHandle();

    TimerHandle(const TimerHandle&) = delete;
    TimerHandle& operator=(const TimerHandle&) = delete;
    TimerHandle(TimerHandle&& other) noexcept;
    TimerHandle& operator=(TimerHandle&& other) noexcept;

    bool Open();
    void Close();
    bool start(uint32_t interval_ms, bool periodic);
    bool stop();
    uint64_t read();

    int getFd() const;
    bool isValid() const;
    bool isPeriodic() const;

private:
    int m_fd{-1};
    bool m_periodic{false};
};

} // namespace Model
} // namespace IpcInterface
