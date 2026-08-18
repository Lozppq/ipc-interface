/**
 * @file EventHandle.cpp
 * @brief eventfd 封装实现
 */

#include "EventHandle.h"
#include "../log/Log_Print.h"
#if defined(__linux__)
#include <sys/eventfd.h>
#include <poll.h>
#include <unistd.h>
#include <cerrno>
#include <cstdint>
#endif

namespace IpcInterface {
namespace Model {

EventHandle::EventHandle() {
#if defined(__linux__)
    m_fd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    if (m_fd < 0) {
        LOG_ERROR("EventHandle::EventHandle eventfd failed");
    }
#endif
}

EventHandle::~EventHandle() {
    Close();
}

bool EventHandle::Open() {
#if defined(__linux__)
    if (m_fd >= 0) {
        return true;
    }
    int new_fd = -1;
    new_fd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    if (new_fd < 0) {
        LOG_ERROR("EventHandle::Open eventfd failed");
        return false;
    }
    m_fd = new_fd;
    return true;
#else
    return false;
#endif
}

void EventHandle::Close() {
#if defined(__linux__)
    if (m_fd >= 0) {
        ::close(m_fd);
        m_fd = -1;
    }
#endif
}

void EventHandle::wake() {
#if defined(__linux__)
    if (m_fd < 0) {
        return;
    }
    uint64_t one = 1;
    ssize_t n = write(m_fd, &one, sizeof(one));
    if (n < 0) {
        LOG_ERROR("EventHandle::wake write failed");
    }
#endif
}

uint64_t EventHandle::wait(int timeout_ms) {
#if defined(__linux__)
    if (m_fd < 0) {
        return 0;
    }
    pollfd pfd{m_fd, POLLIN, 0};
    poll(&pfd, 1, timeout_ms);
    if (pfd.revents & POLLIN) {
        return read();
    }
    return 0;
#else
    (void)timeout_ms;
    return 0;
#endif
}

uint64_t EventHandle::read() {
#if defined(__linux__)
    if (m_fd < 0) {
        return 0;
    }
    uint64_t cnt = 0;
    ssize_t n = ::read(m_fd, &cnt, sizeof(cnt));
    if (n < 0) {
        if (errno != EAGAIN && errno != EWOULDBLOCK) {
            LOG_ERROR("EventHandle::read failed");
        }
        return 0;
    }
    return cnt;
#else
    return 0;
#endif
}

int EventHandle::getFd() const {
    return m_fd;
}

bool EventHandle::isValid() const {
    return m_fd >= 0;
}

EventHandle::EventHandle(EventHandle&& other) noexcept : m_fd(other.m_fd) {
    other.m_fd = -1;
}

EventHandle& EventHandle::operator=(EventHandle&& other) noexcept {
    if (this != &other) {
        Close();
        m_fd = other.m_fd;
        other.m_fd = -1;
    }
    return *this;
}

} // namespace Model
} // namespace IpcInterface
