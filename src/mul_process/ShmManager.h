/**
 * @file ShmManager.h
 * @brief 共享内存管理器
 * @details 基于 POSIX 共享内存实现的无锁环形队列，支持跨进程通信。
 * 通过原子操作实现多生产者单消费者的线程安全，使用信号量实现阻塞通知，
 * 支持崩溃重建（通过 flag 标志位判断初始化状态），数据区大小可配置。
 */

#pragma once
#include "../model/MessageThread.h"
#include "StreamShmCreator.h"
#include "ShmCreator.h"
#include <atomic>
#include <cstdint>
#include <unordered_map>
#include <vector>
#include <string>


namespace IpcInterface {
namespace MulProcess {


typedef struct {
    uint32_t receiver_pid; // 接收者pid
    uint32_t senders_pid; // 发送者pid，0默认有多个发送者
    StreamShmCreator shm; // 共享内存
} ShmInfo;

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

    /**
     * @brief 单例类
     * @return 单例类实例
     */
    static ShmManager& getInstance();

    ShmManager(const ShmManager&) = delete;
    ShmManager& operator=(const ShmManager&) = delete;

    
protected:
    void OnThreadInit() override;

private:
    std::vector<ShmInfo> shmInfos_;
    std::vector<ClientStatusInfo*> clientStatusInfos_;
};

} // namespace MulProcess
} // namespace IpcInterface