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

// 共享内存名称与收发进程 OS pid 映射；sender_pid==0 表示多个发送者
typedef struct {
    std::string m_shm_name;      // 共享内存名称
    uint32_t m_sender_pid;       // 发送者 pid，0 表示多个发送者
    uint32_t m_receiver_pid;     // 接收者 pid
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
     * @param message_id 消息id
     * @param shm_name 共享内存名称，如果为空则使用本进程的消息接口名称
     * @return 是否成功
     */
    bool send(std::vector<uint8_t> msg, uint16_t message_id, std::string shm_name);

    /**
     * @brief 发送消息
     * @param buf_msg 消息数据
     * @param message_id 消息id
     * @param shm_name 共享内存名称，如果为空则使用本进程的消息接口名称
     * @return 是否成功
     */
    bool send(std::shared_ptr<TagSendMessage> buf_msg, std::string shm_name);

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
     * @param pid 进程id
    */
    void handleProcessCrash(uint32_t pid);

    /**
     * @brief 外部线程投递一次请求申请分配共享内存
    */
    void postRequestAllocateShm(std::string sender_shm_name, std::string receiver_shm_name, uint32_t slot_size, uint32_t slot_count, std::string new_shm_name);

    /**
     * @brief 外部线程投递一次请求释放共享内存
    */
    void postRequestReleaseShm(std::string shm_name);

    /**
     * @brief 外部线程投递一次根据共享内存名称创建接收消息线程
    */
    void postCreateReceiveWork(std::string shm_name, ReceiveHandler receive_handler);

    /**
     * @brief 外部线程投递一次根据共享内存名称创建发送消息线程
    */
    void postCreateSendWork(std::string shm_name);

private:
    /**
     * @brief 根据共享内存名称匹配逻辑进程ID
     * @param shm_name 共享内存名称
     * @return 逻辑进程ID，无效返回INVALID_FD
    */
    uint8_t getLogicProcessId(const std::string& shm_name);

    /**
     * @brief 根据进程ID匹配共享内存名称
     * @param pid 进程id
     * @return 共享内存名称，无效返回空字符串
    */
    std::string lookupShmNameByPid(uint32_t pid);


    /**
     * @brief 按逻辑进程槽位查找已登记的接收者 OS pid
     * @param logic_id 逻辑进程槽位
     * @return 接收者 OS pid，无效返回0
     */
    uint32_t lookupReceiverPidByLogicId(uint8_t logic_id) const;

    /**
     * @brief 通过逻辑进程pid寻找已登记的共享内存名称
     * @param logic_id 逻辑进程槽位
     * @return 共享内存名称，无效返回空字符串
     */
    std::string lookupShmNameByLogicId(uint8_t logic_id) const;
    
    /**
     * @brief 请求申请分配共享内存
     * @param sender_shm_name 发送者共享内存名称
     * @param receiver_shm_name 接收者共享内存名称
     * @param slot_size 单槽位大小
     * @param slot_count 槽位数量
     * @param new_shm_name 申请的共享内存名称
     * @return 是否成功申请共享内存
    */
    bool RequestAllocateShm(const std::string& sender_shm_name, const std::string& receiver_shm_name, uint32_t slot_size, uint32_t slot_count, const std::string& new_shm_name);

    /**
     * @brief 请求释放共享内存
     * @param shm_name 共享内存名称
     * @return 是否成功释放共享内存
    */
    bool RequestReleaseShm(const std::string& shm_name);

    /**
     * @brief 根据共享内存名称创建接收消息线程
     * @param shm_name 共享内存名称
     * @param receive_handler 接收消息回调函数
    */
    void createReceiveWork(std::string shm_name, ReceiveHandler receive_handler);
    
    /**
     * @brief 根据共享内存名称创建发送消息线程
     * @param shm_name 共享内存名称
    */
    void createSendWork(std::string shm_name);

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

    /**
     * @brief 初始化发送消息线程
    */
    void initSendWork();

    void openStreamShmRetry(PidNameInfo info, bool create);

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
    using SendWorkMap = std::map<std::string, std::shared_ptr<SendWork>>;
    using ReceiveWorkMap = std::map<std::string, std::shared_ptr<ReceiveWork>>;

    std::shared_ptr<ShmInfoMap> cloneShmInfos() const;
    void storeShmInfos(std::shared_ptr<ShmInfoMap> m);
    std::shared_ptr<SendWorkMap> cloneSendWorks() const;
    void storeSendWorks(std::shared_ptr<SendWorkMap> m);
    std::shared_ptr<ReceiveWorkMap> cloneReceiveWorks() const;
    void storeReceiveWorks(std::shared_ptr<ReceiveWorkMap> m);

    // 无锁快照：单写（本线程）多读（业务 send）
    std::shared_ptr<const ShmInfoMap> m_shm_infos;
    std::shared_ptr<const SendWorkMap> m_send_works;
    std::shared_ptr<const ReceiveWorkMap> m_receive_works;

    std::string m_shm_name;  // 本进程的消息接口名称
    // 接收消息线程对象
    ReceiveWork* m_receive_work{NULL};
    // 默认发送消息线程
    std::shared_ptr<SendWork> m_send_work;
    // 初始化进程id与消息接口名称映射
    std::vector<PidNameInfo> m_pidNameInfos;
    ReceiveHandler m_receive_handler{NULL};
};

} // namespace MulProcess
} // namespace IpcInterface