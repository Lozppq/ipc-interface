/**
 * @file ShmManager.h
 * @brief 共享内存管理器
 * @details 基于 POSIX 共享内存实现的无锁环形队列，支持跨进程通信。
 * 通过原子操作实现多生产者单消费者的线程安全，使用信号量实现阻塞通知，
 * 支持崩溃重建（通过 flag 标志位判断初始化状态），数据区大小可配置。
 */

#pragma once
#include "../model/MessageThread.h"
#include "ShmCreator.h"
#include <atomic>
#include <cstdint>
#include <unordered_map>
#include <vector>
#include <map>
#include <string>
#include <memory>

namespace IpcInterface {
namespace MulProcess {

class ShmManager : public Model::MessageThread {
public:
    /**
     * @brief 构造函数
     * @param name 共享内存名称
     */
    ShmManager();
    
    /**
     * @brief 析构函数，自动调用 close()
     */
    ~ShmManager();

    ShmManager(const ShmManager&) = delete;
    ShmManager& operator=(const ShmManager&) = delete;

    

private:

private:
    std::unordered_map<std::string, std::unique_ptr<ShmCreator>> shm_map_; // 共享内存map
};

} // namespace MulProcess
} // namespace IpcInterface