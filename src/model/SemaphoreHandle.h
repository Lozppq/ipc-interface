/**
 * @file SemaphoreHandle.h
 * @brief Linux unnamed sem_t 包装。ShareProcess 时本对象须落在共享内存里
 */

#pragma once

#if defined(__linux__)
#include <semaphore.h>
#endif

namespace IpcInterface {
namespace Model {

class SemaphoreHandle {
public:
    enum ShareMode {
        ShareThread = 0,
        ShareProcess = 1
    };

    static const int kWaitForever = -1;

    explicit SemaphoreHandle(unsigned int startVal, ShareMode share);
    ~SemaphoreHandle();

    SemaphoreHandle(const SemaphoreHandle&) = delete;
    SemaphoreHandle& operator=(const SemaphoreHandle&) = delete;

    bool post();
    bool wait(int timeoutMs = kWaitForever);
    bool tryWait();
    bool isReady() const;

private:
#if defined(__linux__)
    sem_t m_sem{};
#endif
    bool m_ready{false};
    bool waitForever();
    bool waitTimed(int timeoutMs);
};

} // namespace Model
} // namespace IpcInterface
