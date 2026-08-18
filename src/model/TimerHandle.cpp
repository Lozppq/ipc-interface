/**
 * @file TimerHandle.cpp
 * @brief timerfd 封装实现
 */

#include "TimerHandle.h"
#include "../log/Log_Print.h"
#if defined(__linux__)
#include <sys/timerfd.h>
#include <unistd.h>
#include <cerrno>
#include <cstring>
#endif

namespace IpcInterface {
namespace Model {

TimerHandle::TimerHandle() {
    Open();
}

TimerHandle::~TimerHandle() {
    Close();
}

bool TimerHandle::Open() {
#if defined(__linux__)
    if (m_fd >= 0) {
        return true;
    }
    int new_fd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC);
    if (new_fd < 0) {
        LOG_ERROR("TimerHandle::Open timerfd_create failed: %s", std::strerror(errno));
        return false;
    }
    m_fd = new_fd;
    return true;
#else
    return false;
#endif
}

void TimerHandle::Close() {
#if defined(__linux__)
    if (m_fd >= 0) {
        ::close(m_fd);
        m_fd = -1;
    }
#endif
    m_periodic = false;
}

bool TimerHandle::start(uint32_t interval_ms, bool periodic) {
#if defined(__linux__)
    if (m_fd < 0 || interval_ms == 0) {
        return false;
    }
    itimerspec its{};
    its.it_value.tv_sec = interval_ms / 1000;
    its.it_value.tv_nsec = static_cast<long>((interval_ms % 1000) * 1000000L);
    if (periodic) {
        its.it_interval = its.it_value;
    }
    if (timerfd_settime(m_fd, 0, &its, nullptr) != 0) {
        LOG_ERROR("TimerHandle::start timerfd_settime failed: %s", std::strerror(errno));
        return false;
    }
    m_periodic = periodic;
    return true;
#else
    (void)interval_ms;
    (void)periodic;
    return false;
#endif
}

bool TimerHandle::stop() {
#if defined(__linux__)
    if (m_fd < 0) {
        return true;
    }
    itimerspec its{};
    if (timerfd_settime(m_fd, 0, &its, nullptr) != 0) {
        LOG_ERROR("TimerHandle::stop timerfd_settime failed: %s", std::strerror(errno));
        return false;
    }
    m_periodic = false;
    return true;
#else
    return false;
#endif
}

uint64_t TimerHandle::read() {
#if defined(__linux__)
    if (m_fd < 0) {
        return 0;
    }
    uint64_t cnt = 0;
    ssize_t n = ::read(m_fd, &cnt, sizeof(cnt));
    if (n < 0) {
        if (errno != EAGAIN && errno != EWOULDBLOCK) {
            LOG_ERROR("TimerHandle::read failed: %s", std::strerror(errno));
        }
        return 0;
    }
    return cnt;
#else
    return 0;
#endif
}

int TimerHandle::getFd() const {
    return m_fd;
}

bool TimerHandle::isValid() const {
    return m_fd >= 0;
}

bool TimerHandle::isPeriodic() const {
    return m_periodic;
}

TimerHandle::TimerHandle(TimerHandle&& other) noexcept
    : m_fd(other.m_fd), m_periodic(other.m_periodic) {
    other.m_fd = -1;
    other.m_periodic = false;
}

TimerHandle& TimerHandle::operator=(TimerHandle&& other) noexcept {
    if (this != &other) {
        Close();
        m_fd = other.m_fd;
        m_periodic = other.m_periodic;
        other.m_fd = -1;
        other.m_periodic = false;
    }
    return *this;
}

} // namespace Model
} // namespace IpcInterface
