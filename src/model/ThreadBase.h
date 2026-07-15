/**
 * @file ThreadBase.h
 * @brief 线程基类
 * @details 封装线程创建、启动、停止的通用逻辑，提供 OnThreadInit 初始化钩子，
 * 派生类通过重写纯虚函数 Run() 实现具体业务逻辑，支持线程生命周期管理。
 */

#pragma once

#include <atomic>
#include <string>
#include <thread>

namespace IpcInterface {
namespace Model {
class ThreadBase {
public:
    /**
     * @brief 构造函数
     * @param name 工作线程名，Linux 最多 15 字符；空串表示不设置
     */
    explicit ThreadBase(std::string name = {});
    
    /**
     * @brief 析构函数，确保线程停止
     */
    ~ThreadBase();

    ThreadBase(const ThreadBase&) = delete;
    ThreadBase& operator=(const ThreadBase&) = delete;

    /**
     * @brief 启动线程
     */
    void start();
    
    /**
     * @brief 停止线程（阻塞等待）
     */
    virtual void stop();
    
    /**
     * @brief 查询线程运行状态
     * @return 运行中返回true，否则返回false
     */
    bool isRunning() const;

    /**
     * @brief 设置运行标志
     */
    void setRunning(bool running);

    /**
     * @brief 等待线程退出
     */
    void wait();

protected:
    /**
     * @brief 线程初始化钩子，在线程启动后、Run()执行前调用
     */
    virtual void OnThreadInit() {}
    
    /**
     * @brief 线程主循环函数，派生类必须实现
     */
    virtual void Run() = 0;
    
    /**
     * @brief 查询是否在工作线程中执行
     * @return 在工作线程中执行返回true，否则返回false
     */
    bool isInWorkerThread() const;

private:
    /**
     * @brief 线程入口函数，封装初始化和循环逻辑
     */
    void threadFunc();
    void applyThreadName();

    std::thread thread_;
    std::thread::id worker_thread_id_;
    std::atomic<bool> running_{false};
    std::string thread_name_;
};

} // namespace Model
} // namespace IpcInterface