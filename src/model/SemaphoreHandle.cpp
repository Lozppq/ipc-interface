/**
 * @file SemaphoreHandle.cpp
 * @brief unnamed sem_t 包装实现
 */

#include "SemaphoreHandle.h"
#include "../log/Log_Print.h"
#include <cerrno>
#include <cstring>
#if defined(__linux__)
#include <ctime>
#endif

namespace IpcInterface
{
namespace Model
{

SemaphoreHandle::SemaphoreHandle(unsigned int startVal, ShareMode share)
    : m_ready(false)
{
#if defined(__linux__)
    const int pshared = (share == ShareProcess) ? 1 : 0;
    if (sem_init(&m_sem, pshared, startVal) != 0)
    {
        LOG_ERROR("SemaphoreHandle: sem_init failed share=%d: %s", pshared, std::strerror(errno));
        return;
    }
    m_ready = true;
#else
    (void)startVal;
    (void)share;
    LOG_WARN("SemaphoreHandle: not supported on this platform");
#endif
}

SemaphoreHandle::~SemaphoreHandle()
{
#if defined(__linux__)
    if (!m_ready)
        return;
    if (sem_destroy(&m_sem) != 0)
        LOG_ERROR("SemaphoreHandle: sem_destroy failed: %s", std::strerror(errno));
    m_ready = false;
#endif
}

bool SemaphoreHandle::post()
{
#if defined(__linux__)
    if (!m_ready)
        return false;
    if (sem_post(&m_sem) != 0)
    {
        LOG_ERROR("SemaphoreHandle: sem_post failed: %s", std::strerror(errno));
        return false;
    }
    return true;
#else
    return false;
#endif
}

bool SemaphoreHandle::wait(int timeoutMs)
{
#if defined(__linux__)
    if (!m_ready)
        return false;
    if (timeoutMs == kWaitForever)
        return waitForever();
    if (timeoutMs < 0)
    {
        LOG_ERROR("SemaphoreHandle: wait invalid timeoutMs=%d", timeoutMs);
        return false;
    }
    return waitTimed(timeoutMs);
#else
    (void)timeoutMs;
    return false;
#endif
}

bool SemaphoreHandle::waitForever()
{
#if defined(__linux__)
    int waitRet = -1;
    do
        waitRet = sem_wait(&m_sem);
    while (waitRet != 0 && errno == EINTR);
    if (waitRet != 0)
    {
        LOG_ERROR("SemaphoreHandle: sem_wait failed: %s", std::strerror(errno));
        return false;
    }
    return true;
#else
    return false;
#endif
}

bool SemaphoreHandle::waitTimed(int timeoutMs)
{
#if defined(__linux__)
    struct timespec absTs;
    if (clock_gettime(CLOCK_REALTIME, &absTs) != 0)
    {
        LOG_ERROR("SemaphoreHandle: clock_gettime failed: %s", std::strerror(errno));
        return false;
    }
    absTs.tv_sec += timeoutMs / 1000;
    absTs.tv_nsec += (timeoutMs % 1000) * 1000000L;
    if (absTs.tv_nsec >= 1000000000L)
    {
        absTs.tv_sec += 1;
        absTs.tv_nsec -= 1000000000L;
    }

    int timedRet = -1;
    do
        timedRet = sem_timedwait(&m_sem, &absTs);
    while (timedRet != 0 && errno == EINTR);
    if (timedRet == 0)
        return true;
    if (errno == ETIMEDOUT)
        return false;
    LOG_ERROR("SemaphoreHandle: sem_timedwait failed: %s", std::strerror(errno));
    return false;
#else
    (void)timeoutMs;
    return false;
#endif
}

bool SemaphoreHandle::tryWait()
{
#if defined(__linux__)
    if (!m_ready)
        return false;
    if (sem_trywait(&m_sem) == 0)
        return true;
    if (errno != EAGAIN)
        LOG_ERROR("SemaphoreHandle: sem_trywait failed: %s", std::strerror(errno));
    return false;
#else
    return false;
#endif
}

bool SemaphoreHandle::isReady() const
{
    return m_ready;
}

} // namespace Model
} // namespace IpcInterface
