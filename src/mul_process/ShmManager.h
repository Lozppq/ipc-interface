/**
 * @file ShmManager.h
 * @brief 共享内存管理器
 * @details 基于 POSIX 共享内存实现的无锁环形队列，支持跨进程通信。
 * 通过原子操作实现多生产者单消费者的线程安全，使用信号量实现阻塞通知，
 * 支持崩溃重建（通过 flag 标志位判断初始化状态），数据区大小可配置。
 */

#ifndef SHM_MANAGER_H
#define SHM_MANAGER_H

#include <atomic>
#include <cstdint>
#include <semaphore.h>

// 消息头部掩码，低31位存储长度，最高位为提交标志
#define MSG_HEADER_MASK  0x7FFFFFFF
#define MSG_COMMIT_BIT   0x80000000

/**
 * @brief 环形队列头部结构，存储在共享内存起始位置
 */
typedef struct {
    sem_t sem __attribute__((aligned(4)));           // 信号量，用于消费者阻塞等待
    std::atomic<uint32_t> head __attribute__((aligned(4)));  // 队头指针
    std::atomic<uint32_t> tail __attribute__((aligned(4)));  // 队尾指针
    std::atomic<uint32_t> flag __attribute__((aligned(4)));  // 标志位（如允许写入标志）
    std::atomic<uint32_t> data_size __attribute__((aligned(4))); // 数据区大小
    uint8_t data[0];  // 柔性数组成员，指向共享内存数据区
} RingQueueHeader;

class ShmManager {
public:
    /**
     * @brief 队列大小枚举
     */
    enum class QueueSize : uint32_t {
        SIZE_ATTACH = 0,    // 仅打开不指定大小
        SIZE_1MB = 1 * 1024 * 1024,
        SIZE_2MB = 2 * 1024 * 1024,
        SIZE_3MB = 3 * 1024 * 1024,
        SIZE_4MB = 4 * 1024 * 1024,
        SIZE_5MB = 5 * 1024 * 1024
    };

    /**
     * @brief 标志位枚举
     */
    enum class Flag : uint32_t {
        FLAG_ALLOW_WRITE = 0x01, // bit0: 允许写入
    };

    /**
     * @brief 构造函数
     * @param name 共享内存名称
     */
    ShmManager(const char *name);
    
    /**
     * @brief 析构函数，自动调用 close()
     */
    ~ShmManager();

    ShmManager(const ShmManager&) = delete;
    ShmManager& operator=(const ShmManager&) = delete;

    /**
     * @brief 打开共享内存
     * @param size 队列大小（仅 create=true 时有效）
     * @param create true=创建模式，false=仅打开模式
     * @return 成功返回true，失败返回false
     */
    bool open(QueueSize size, bool create);
    
    /**
     * @brief 关闭共享内存，释放资源
     */
    void close();
    
    /**
     * @brief 检查是否有效
     * @return 有效返回true，否则返回false
     */
    bool valid() const;

    /**
     * @brief 发送消息
     * @param msg 消息数据指针
     * @param len 消息长度
     * @return 成功返回0，失败返回-1
     */
    int send(const uint8_t *msg, uint32_t len);
    
    /**
     * @brief 接收消息
     * @param buf 接收缓冲区
     * @param buf_len 缓冲区大小
     * @return 接收字节数，失败返回0
     */
    uint32_t recv(uint8_t *buf, uint32_t buf_len);

    /**
     * @brief 判断队列是否为空
     * @return 空返回true，否则返回false
     */
    bool is_empty();
    
    /**
     * @brief 判断队列是否已满
     * @param len 待写入数据长度
     * @return 满返回true，否则返回false
     */
    bool is_full(uint32_t len);

private:
    bool ring_is_empty(RingQueueHeader *q);
    bool ring_is_full(RingQueueHeader *q, uint32_t queue_size, uint32_t len);
    int ring_enqueue(RingQueueHeader *q, uint32_t queue_size, const uint8_t *msg, uint32_t len);
    uint32_t ring_dequeue(RingQueueHeader *q, uint32_t queue_size, uint8_t *buf, uint32_t buf_len);
    void ring_init(RingQueueHeader *q, uint32_t queue_size);
    void ring_destroy(RingQueueHeader *q);
    void ring_reset(RingQueueHeader *q, uint32_t queue_size);

private:
    int shm_fd_;           // 共享内存文件描述符
    RingQueueHeader *q_;   // 队列头部指针
    bool owner_;           // 是否为创建方
    uint32_t queue_size_;  // 数据区大小
    char shm_name_[64];    // 共享内存名称
};

#endif