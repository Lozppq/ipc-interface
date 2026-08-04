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
#include "../model/ShmCreator.h"
#include "ReceiveWork.h"
#include "SendWork.h"
#include <atomic>
#include <cstdint>
#include <unordered_map>
#include <vector>
#include <string>
#include <memory>
#include <map>


namespace IpcInterface {
namespace MulProcess {

// 存储名称和pid，这个是初始化进程id与消息接口名称的映射，用于初始化各个共享内存
typedef struct {
    std::string shm_name;  // 共享内存名称
    uint32_t pid;  // 进程id
} PidNameInfo;

class ShmManager : public Model::MessageThread {
public:
    /**
     * @brief 构造函数
     * @param shm_name 共享内存名称，作为本进程的消息接口名称
     */
    ShmManager();
    
    /**
     * @brief 析构函数，自动调用 close()
     */
    ~ShmManager();

    /**
     * @brief 初始化参数
     * @param shm_name 共享内存名称
     */
    void initParams(const std::string& shm_name);

    /**
     * @brief 单例类
     * @return 单例类指针
     */
    static ShmManager* getInstance();

    ShmManager(const ShmManager&) = delete;
    ShmManager& operator=(const ShmManager&) = delete;

    /**
     * @brief 发送消息
     * @param msg 消息数据
     */
    void send(std::vector<uint8_t>& msg, std::string shm_name);

    /**
     * @brief 接收线程消息回调
    */
    void onReceiveMessage(std::shared_ptr<TagReceiveMessage> tag);

    /**
     * @brief 添加初始化进程id与消息接口名称映射
     * @param info 进程id与消息接口名称映射
     */
    void addPidNameInfo(PidNameInfo info);

    /**
     * @brief 外部线程投递一次创建一个pidinfor相对应的共享内存
    */
    void postCreatePidNameInfo(PidNameInfo info);

    /**
     * @brief 处理进程崩溃共享内存的重置
     * @param pid 进程id
    */
    void handleProcessCrash(uint32_t pid);

    /**
     * @brief 根据共享内存名称匹配逻辑进程ID
     * @param shm_name 共享内存名称
     * @return 逻辑进程ID，无效返回INVALID_FD
    */
    uint8_t getLogicProcessId(const std::string& shm_name);
    

protected:
    void OnThreadInit() override;

private:
    /**
     * @brief 初始化各个共享内存
     * @param create 是否创建共享内存
    */
    void initShm(bool create);

    /**
     * @brief 初始化接收消息线程
    */
    void initReceiveWork();

    /**
     * @brief 初始化发送消息线程
    */
    void initSendWork();

    void openStreamShmRetry(PidNameInfo info, bool create);

    std::map<std::string, std::unique_ptr<StreamShmCreator>> shmInfosMap_;

    // 真实pid映射到逻辑进程ID的映射
    std::map<uint32_t, uint8_t> realPidToLogicProcessIdMap_;
    std::string shm_name_;  // 本进程的消息接口名称
    // 接收消息线程对象
    ReceiveWork* receive_work_{NULL};
    // 发送消息线程对象
    SendWork* send_work_{NULL};
    // 初始化进程id与消息接口名称映射
    std::vector<PidNameInfo> pidNameInfos_;
};

} // namespace MulProcess
} // namespace IpcInterface