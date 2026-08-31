/**
 * @file EpollHandle.h
 * @brief epoll 封装：add/mod/del/wait，wait 结果写入 m_epEvents
 */

#pragma once

#include <cstdint>
#if defined(__linux__)
#include <sys/epoll.h>
#endif

namespace IpcInterface {
namespace Model {

class EpollHandle {
public:
#if defined(__linux__)
    static constexpr int kMaxEventsOnce = 64;
#endif

    EpollHandle();
    ~EpollHandle();

    EpollHandle(const EpollHandle&) = delete;
    EpollHandle& operator=(const EpollHandle&) = delete;

    bool add(int fd, uint32_t events);
    bool mod(int fd, uint32_t events);
    bool del(int fd);

    // timeout_ms: -1 永久等待，0 非阻塞，>0 超时毫秒；返回就绪个数，失败 -1
    int wait(int timeout_ms);
#if defined(__linux__)
    const epoll_event* events() const { return m_epEvents; }
#endif
    int readyCount() const { return m_ready_count; }

    void Close();
    bool isValid() const;

private:
    int m_epfd{-1};
    int m_ready_count{0};
#if defined(__linux__)
    epoll_event m_epEvents[kMaxEventsOnce];
#endif
};

} // namespace Model
} // namespace IpcInterface
