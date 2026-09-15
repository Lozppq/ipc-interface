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
#include "ReceiveWork.h"
#include <atomic>
#include <cstdint>
#include <unordered_map>
#include <vector>
#include <string>
#include <memory>
#include <map>
#include <functional>

namespace IpcInterface
{
namespace MulProcess
{

// 共享内存名称与逻辑进程槽位映射；sender==INVALID_FD 表示多个发送者
typedef struct
{
    std::string m_shm_name;      // 共享内存名称
    uint8_t m_sender_logic;      // 发送者逻辑槽位，INVALID_FD 表示多个发送者
    uint8_t m_receiver_logic;    // 接收者逻辑槽位
} PidNameInfo;

using SyncFlagCallback = std::function<void(uint8_t logic_id, uint8_t flag)>;
using StartProcessCallback = std::function<void(std::string shm_name, uint8_t logic_id)>;

class ShmManager : public Model::MessageThread
{
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
     * @brief 发送消息（调用线程直发）
     */
    bool send(const std::shared_ptr<TagSendMessage>& buf_msg, const std::string& shm_name);
    std::shared_ptr<TagSendMessage> makeSendMessage(std::vector<uint8_t>&& data, uint16_t message_id);

    /**
     * @brief 设置接收消息回调函数
    */
    void setReceiveHandler(ReceiveHandler handler);

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
     * @param logic_id 逻辑进程槽位
    */
    void handleProcessCrash(uint8_t logic_id);

    /**
     * @brief 请求申请分配共享内存（可任意线程直调）
    */
    bool RequestAllocateShm(const std::string& sender_shm_name,
        const std::string& receiver_shm_name, uint32_t slot_size, uint32_t slot_count,
        const std::string& new_shm_name);

    /**
     * @brief 请求释放共享内存（可任意线程直调）
    */
    bool RequestReleaseShm(const std::string& shm_name);

    /**
     * @brief 按共享内存名称创建接收线程；已存在则返回已有实例，shm 未就绪返回空
    */
    std::shared_ptr<ReceiveWork> createReceiveWork(std::string shm_name, ReceiveHandler receive_handler);

    /**
     * @brief 固定通道 shm 创建完成后回调进程管理模块拉起对应进程（仅守护进程）
    */
    void setStartProcessCallback(StartProcessCallback callback);

    /**
     * @brief 恢复固定通道收发标志（进程即将拉起时调用）
    */
    void enableChannel(const std::string& shm_name);

    /**
     * @brief 设置同步标志回调函数
    */
    void setSyncFlagCallback(SyncFlagCallback callback);

    /**
     * @brief 设置同步标志（可任意线程直调）
     * @param shm_name 共享内存名称
     * @param flag 同步标志
     * @return 是否成功
    */
    bool setSyncFlag(std::string shm_name, uint8_t flag);

private:
    /**
     * @brief 根据共享内存名称匹配逻辑进程ID
     * @param shm_name 共享内存名称
     * @return 逻辑进程ID，无效返回INVALID_FD
    */
    uint8_t getLogicProcessId(const std::string& shm_name);

    /**
     * @brief 通过逻辑进程pid寻找已登记的共享内存名称
     * @param logic_id 逻辑进程槽位
     * @return 共享内存名称，无效返回空字符串
     */
    std::string lookupShmNameByLogicId(uint8_t logic_id) const;

protected:
    void OnThreadInit() override;

private:
    /**
     * @brief 接收线程消息回调
    */
    void onReceiveMessage(std::shared_ptr<TagReceiveMessage> tag);

    /**
     * @brief 初始化各个共享内存
     * @param create 是否创建共享内存
    */
    void initShm(bool create);

    /**
     * @brief 初始化接收消息线程
    */
    void initReceiveWork();

    void openStreamShmRetry(PidNameInfo info, bool create);
    void tryStartFixedProcesses();

    /**
     * @brief 处理守护进程消息
     * @param tag 消息数据
    */
    void handleDaemonMessage(std::shared_ptr<TagReceiveMessage> tag);

    /**
     * @brief 处理业务进程消息
     * @param tag 消息数据
    */
    void handleProcessMessage(std::shared_ptr<TagReceiveMessage> tag);

    using ShmInfoMap = std::map<std::string, std::shared_ptr<StreamShmCreator>>;
    using ReceiveWorkMap = std::map<std::string, std::shared_ptr<ReceiveWork>>;

    std::shared_ptr<const ShmInfoMap> shmInfos() const;
    bool addShmInfo(const std::string& name, std::shared_ptr<StreamShmCreator> shm);
    std::shared_ptr<StreamShmCreator> removeShmInfo(const std::string& name);

    std::shared_ptr<const ReceiveWorkMap> receiveWorks() const;
    bool addReceiveWork(const std::string& name, std::shared_ptr<ReceiveWork> work);
    std::shared_ptr<ReceiveWork> removeReceiveWork(const std::string& name);

    // 无锁快照：CAS 增删，读路径持有 const 快照
    std::shared_ptr<const ShmInfoMap> m_shm_infos;
    std::shared_ptr<const ReceiveWorkMap> m_receive_works;

    std::string m_shm_name;  // 本进程的消息接口名称
    // 初始化进程id与消息接口名称映射
    std::vector<PidNameInfo> m_pidNameInfos;
    ReceiveHandler m_receive_handler{NULL};
    SyncFlagCallback m_sync_flag_callback{NULL};
    StartProcessCallback m_start_process_callback{NULL};
    bool m_fixed_processes_started{false};
};

} // namespace MulProcess
} // namespace IpcInterface
