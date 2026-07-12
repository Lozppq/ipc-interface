#pragma once

#include <atomic>
#include <cstdint>
#include <semaphore.h>
#include <vector>
#include <string>

namespace IpcInterface {
namespace MulProcess {
#define SMALL_DATA_SLOT_SIZE 64
#define MEDIUM_DATA_SLOT_SIZE 1024
#define LARGE_DATA_SLOT_SIZE (1024 * 256)

typedef struct {
    std::atomic<uint8_t> slice_id;  // 切片id
    std::atomic<uint8_t> slice_count;  // 切片数量
    std::atomic<bool> commit;  // 提交标志位
    std::atomic<uint8_t> modify_id;  // 修改者id
    uint8_t data[SMALL_DATA_SLOT_SIZE];
}SMALLDataSlot;

typedef struct {
    std::atomic<uint8_t> slice_id;  // 切片id
    std::atomic<uint8_t> slice_count;  // 切片数量
    std::atomic<bool> commit;  // 提交标志位
    std::atomic<uint8_t> modify_id;  // 修改者id
    uint8_t data[MEDIUM_DATA_SLOT_SIZE];
}MEDIUMDataSlot;

typedef struct {
    std::atomic<uint8_t> slice_id;  // 切片id
    std::atomic<uint8_t> slice_count;  // 切片数量
    std::atomic<bool> commit;  // 提交标志位
    std::atomic<uint8_t> modify_id;  // 修改者id
    uint8_t data[LARGE_DATA_SLOT_SIZE];
}LARGEDataSlot;


/**
 * @brief 小数据环形队列结构体
**/
typedef struct {
    sem_t sem;           // 信号量，用于消费者阻塞等待
    std::atomic<uint32_t> head;  // 队头指针
    std::atomic<uint32_t> tail;  // 队尾指针
    std::atomic<uint32_t> slot_size; // 数据区大小
    std::atomic<uint32_t> flag; // 标志位
    // bit0：1允许发送，0不允许发送
    SMALLDataSlot data[0];  // 柔性数组成员，指向共享内存数据区
} SMALLRingQueueHeader;

/**
 * @brief 中数据环形队列结构体
**/
typedef struct {
    sem_t sem;           // 信号量，用于消费者阻塞等待
    std::atomic<uint32_t> head;  // 队头指针
    std::atomic<uint32_t> tail;  // 队尾指针
    std::atomic<uint32_t> slot_size; // 数据区大小
    std::atomic<uint32_t> flag; // 标志位
    // bit0：1允许发送，0不允许发送
    MEDIUMDataSlot data[0];  // 柔性数组成员，指向共享内存数据区
} MEDIUMRingQueueHeader;

/**
 * @brief 大数据环形队列结构体
**/
typedef struct {
    sem_t sem;           // 信号量，用于消费者阻塞等待
    std::atomic<uint32_t> head;  // 队头指针
    std::atomic<uint32_t> tail;  // 队尾指针
    std::atomic<uint32_t> slot_size; // 数据区大小
    std::atomic<uint32_t> flag; // 标志位
    // bit0：1允许发送，0不允许发送
    LARGEDataSlot data[0];  // 柔性数组成员，指向共享内存数据区
} LARGERingQueueHeader;

/**
 * @brief 客户端状态信息结构体
**/
typedef struct {
    std::atomic<uint32_t> send_tail;  // 维护一个最后发送消息的尾部索引
    std::atomic<uint32_t> send_head;  // 维护一个最后发送消息的头部索引
    std::atomic<uint8_t> send_status;  // 维护一个最后发送消息的状态
    std::atomic<uint8_t> send_object_id;  // 维护一个最后发送消息的对象id
}ClientStatusInfo;

/**
* @brief 可以支持的数据区大小枚举
*/
enum class SlotSize : uint32_t {
    SIZE_64B = 64,
    SIZE_1KB = 1024,
    SIZE_256KB = 256 * 1024,
};

class ShmCreator {
public:
    /**
     * @brief 构造函数
     * @param name 共享内存名称
     */
    ShmCreator(const std::string& name, SlotSize size = SlotSize::SIZE_64B, uint32_t slot_count = 1024);
    
    /**
     * @brief 析构函数，自动调用 close()
     */
    ~ShmCreator();

    ShmCreator(const ShmCreator&) = delete;
    ShmCreator& operator=(const ShmCreator&) = delete;

    /**
     * @brief 打开共享内存
     * @param size 队列大小（仅 create=true 时有效）
     * @param create true=创建模式，false=仅打开模式
     * @return 成功返回true，失败返回false
     */
    bool open();
    
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
     * @param msg 消息数据
     * @return 成功返回0，失败返回-1
     */
    int send(const std::vector<uint8_t>& msg);
    
    /**
     * @brief 接收消息，按消息实际长度调整 buf 并写入数据
     * @param buf 接收缓冲区，内部会 resize 到消息长度
     * @return 接收字节数，失败返回0
     */
    uint32_t recv(std::vector<uint8_t>& buf);

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
    /**
     * @brief 创建共享内存结构体
     */
     void create_shm(bool create);
    
    /**
     * @brief 删除共享内存
     */
    void delete_shm();

private:
    std::string shm_name_;
    SlotSize slot_size_;
    uint32_t slot_count_;
    // 共享内存总大小
    uint32_t total_size_;
    int shm_fd_;
    void* shm_ptr_;
    bool is_owner_;
};

} // namespace MulProcess
} // namespace IpcInterface