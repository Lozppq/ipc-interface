/**
 * @file EpollControl.cpp
 * @brief epoll 控制器实现
 */

#include "EpollControl.h"
#include "../log/Log_Print.h"
#if defined(__linux__)
#include <sys/epoll.h>
#endif

namespace IpcInterface
{
namespace Model
{

EpollControl::EpollControl(size_t queue_size)
    : m_queue(queue_size)
{
#if defined(__linux__)
    if (m_event.isValid())
        m_epoll.add(m_event.getFd(), EPOLLIN | EPOLLET);
#endif
}

EpollControl::~EpollControl()
{
    while (!m_timers.empty())
        stopTimer(m_timers.begin()->first);
    while (!m_fds.empty())
        removeFd(m_fds.begin()->first);
}

bool EpollControl::post(std::function<void()> callback)
{
    if (!callback)
        return false;
    if (m_queue.push(std::move(callback)))
    {
        m_event.wake();
        return true;
    }
    return false;
}

int EpollControl::startTimer(uint32_t interval_ms, bool periodic, TimerCallback callback)
{
    TimerItem item;
    if (!item.m_timer.start(interval_ms, periodic))
        return -1;
    int fd = item.m_timer.getFd();
#if defined(__linux__)
    if (!m_epoll.add(fd, EPOLLIN | EPOLLET))
        return -1;
#else
    (void)fd;
    return -1;
#endif
    item.m_callback = std::move(callback);
    m_timers.emplace(fd, std::move(item));
    return fd;
}

bool EpollControl::addFd(int fd, FdCallback callback)
{
    if (fd < 0 || !callback || m_fds.count(fd) || m_timers.count(fd))
        return false;
#if defined(__linux__)
    if (!m_epoll.add(fd, EPOLLIN))
        return false;
#else
    (void)fd;
    return false;
#endif
    m_fds.emplace(fd, std::move(callback));
    return true;
}

void EpollControl::removeFd(int fd)
{
    auto it = m_fds.find(fd);
    if (it == m_fds.end())
        return;
    m_epoll.del(fd);
    m_fds.erase(it);
}

void EpollControl::stopTimer(int fd)
{
    auto it = m_timers.find(fd);
    if (it == m_timers.end())
        return;
    m_epoll.del(fd);
    it->second.m_timer.stop();
    m_timers.erase(it);
}

int EpollControl::wait(int timeout_ms)
{
    int n = m_epoll.wait(timeout_ms);
    if (n <= 0)
        return n;
#if defined(__linux__)
    const epoll_event* evs = m_epoll.events();
    for (int i = 0; i < n; ++i)
    {
        int fd = evs[i].data.fd;
        if (fd == m_event.getFd())
        {
            m_event.read();
            std::function<void()> task;
            while (m_queue.pop(task))
            {
                if (task)
                    task();
            }
            continue;
        }
        auto tit = m_timers.find(fd);
        if (tit != m_timers.end())
        {
            tit->second.m_timer.read();
            TimerCallback cb = tit->second.m_callback;
            const bool periodic = tit->second.m_timer.isPeriodic();
            if (cb)
                cb(fd);
            if (!periodic)
                stopTimer(fd);
            continue;
        }
        auto fit = m_fds.find(fd);
        if (fit == m_fds.end())
            continue;
        FdCallback cb = fit->second;
        if (cb)
            cb(fd);
    }
#endif
    return n;
}

void EpollControl::wake()
{
    m_event.wake();
}

} // namespace Model
} // namespace IpcInterface
