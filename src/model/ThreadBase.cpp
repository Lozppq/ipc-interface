/**
 * @file ThreadBase.cpp
 * @brief 线程基类实现
 * @details 实现线程的启动、停止和生命周期管理，通过原子标志位保证线程安全，
 * 线程入口函数先调用 OnThreadInit 初始化，再循环执行 Run() 直到停止。
 */

#include "ThreadBase.h"

#if defined(__linux__)
#include <pthread.h>
#endif

namespace IpcInterface {
namespace Model {

ThreadBase::ThreadBase(std::string name) : m_thread_name(std::move(name)) {}

ThreadBase::~ThreadBase() {
    stop();
}

void ThreadBase::applyThreadName() {
    if (m_thread_name.empty()) {
        return;
    }
#if defined(__linux__)
    pthread_setname_np(pthread_self(), m_thread_name.substr(0, 15).c_str());
#endif
}

void ThreadBase::start() {
    // 双重检查，避免重复启动
    if (!m_running.load(std::memory_order_acquire)) {
        m_running.store(true, std::memory_order_release);
        m_thread = std::thread(&ThreadBase::threadFunc, this);
    }
}

void ThreadBase::stop() {
    // 设置停止标志，等待线程退出
    if (m_running.load(std::memory_order_acquire)) {
        setRunning(false);
        wait();
    }
}

bool ThreadBase::isRunning() const {
    return m_running.load(std::memory_order_acquire);
}

void ThreadBase::setRunning(bool running) {
    m_running.store(running, std::memory_order_release);
}

void ThreadBase::wait() {
    if (m_thread.joinable()) {
        m_thread.join();
    }
}

bool ThreadBase::isInWorkerThread() const {
    return std::this_thread::get_id() == m_worker_thread_id;
}

void ThreadBase::threadFunc() {
    m_worker_thread_id = std::this_thread::get_id();
    applyThreadName();
    OnThreadInit();
    // 循环执行 Run()，直到 m_running 被设置为 false
    while (isRunning()) {
        Run();
    }
    OnThreadExit();
}

} // namespace Model
} // namespace IpcInterface