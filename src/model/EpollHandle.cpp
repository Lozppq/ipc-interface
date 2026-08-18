/**
 * @file EpollHandle.cpp
 * @brief epoll 封装实现
 */

#include "EpollHandle.h"
#include "../log/Log_Print.h"
#if defined(__linux__)
#include <unistd.h>
#include <cerrno>
#include <cstring>
#endif

namespace IpcInterface {
namespace Model {

EpollHandle::EpollHandle() {
#if defined(__linux__)
    m_epfd = epoll_create1(EPOLL_CLOEXEC);
    if (m_epfd < 0) {
        LOG_ERROR("EpollHandle::EpollHandle epoll_create1 failed: %s", std::strerror(errno));
    }
    std::memset(m_epEvents, 0, sizeof(m_epEvents));
#endif
}

EpollHandle::~EpollHandle() {
    close();
}

void EpollHandle::close() {
#if defined(__linux__)
    if (m_epfd >= 0) {
        ::close(m_epfd);
        m_epfd = -1;
    }
#endif
    m_ready_count = 0;
}

bool EpollHandle::isValid() const {
    return m_epfd >= 0;
}

bool EpollHandle::add(int fd, uint32_t events) {
#if defined(__linux__)
    if (m_epfd < 0 || fd < 0) {
        return false;
    }
    epoll_event ev{};
    ev.events = events;
    ev.data.fd = fd;
    if (epoll_ctl(m_epfd, EPOLL_CTL_ADD, fd, &ev) != 0) {
        LOG_ERROR("EpollHandle::add fd=%d failed: %s", fd, std::strerror(errno));
        return false;
    }
    return true;
#else
    (void)fd;
    (void)events;
    return false;
#endif
}

bool EpollHandle::mod(int fd, uint32_t events) {
#if defined(__linux__)
    if (m_epfd < 0 || fd < 0) {
        return false;
    }
    epoll_event ev{};
    ev.events = events;
    ev.data.fd = fd;
    if (epoll_ctl(m_epfd, EPOLL_CTL_MOD, fd, &ev) != 0) {
        LOG_ERROR("EpollHandle::mod fd=%d failed: %s", fd, std::strerror(errno));
        return false;
    }
    return true;
#else
    (void)fd;
    (void)events;
    return false;
#endif
}

bool EpollHandle::del(int fd) {
#if defined(__linux__)
    if (m_epfd < 0 || fd < 0) {
        return false;
    }
    if (epoll_ctl(m_epfd, EPOLL_CTL_DEL, fd, nullptr) != 0) {
        if (errno != ENOENT) {
            LOG_ERROR("EpollHandle::del fd=%d failed: %s", fd, std::strerror(errno));
            return false;
        }
    }
    return true;
#else
    (void)fd;
    return false;
#endif
}

int EpollHandle::wait(int timeout_ms) {
    m_ready_count = 0;
#if defined(__linux__)
    if (m_epfd < 0) {
        return -1;
    }
    int n = epoll_wait(m_epfd, m_epEvents, kMaxEventsOnce, timeout_ms);
    if (n < 0) {
        if (errno == EINTR) {
            return 0;
        }
        LOG_ERROR("EpollHandle::wait failed: %s", std::strerror(errno));
        return -1;
    }
    m_ready_count = n;
    return n;
#else
    (void)timeout_ms;
    return 0;
#endif
}

} // namespace Model
} // namespace IpcInterface
