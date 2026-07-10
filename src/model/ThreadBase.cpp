/**
 * @file ThreadBase.cpp
 * @brief 线程基类实现
 * @details 实现线程的启动、停止和生命周期管理，通过原子标志位保证线程安全，
 * 线程入口函数先调用 OnThreadInit 初始化，再循环执行 Run() 直到停止。
 */

#include "ThreadBase.h"

namespace IpcInterface {
namespace Model {

ThreadBase::ThreadBase() {}

ThreadBase::~ThreadBase() {
    stop();
}

void ThreadBase::start() {
    // 双重检查，避免重复启动
    if (!running_.load(std::memory_order_acquire)) {
        running_.store(true, std::memory_order_release);
        thread_ = std::thread(&ThreadBase::threadFunc, this);
    }
}

void ThreadBase::stop() {
    // 设置停止标志，等待线程退出
    if (running_.load(std::memory_order_acquire)) {
        setRunning(false);
        wait();
    }
}

bool ThreadBase::isRunning() const {
    return running_.load(std::memory_order_acquire);
}

void ThreadBase::setRunning(bool running) {
    running_.store(running, std::memory_order_release);
}

void ThreadBase::wait() {
    if (thread_.joinable()) {
        thread_.join();
    }
}

bool ThreadBase::isInWorkerThread() const {
    return std::this_thread::get_id() == worker_thread_id_;
}

void ThreadBase::threadFunc() {
    worker_thread_id_ = std::this_thread::get_id();
    OnThreadInit();
    // 循环执行 Run()，直到 running_ 被设置为 false
    while (isRunning()) {
        Run();
    }
}

} // namespace Model
} // namespace IpcInterface