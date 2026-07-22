#pragma once

#include <atomic>
#include <cstdint>
#include <cstring>
#include <semaphore.h>
#include <vector>
#include <string>
#include "../define/common.h"

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
    std::atomic<uint32_t> slot_count; // 数据区数量
    std::atomic<uint32_t> receiver_pid;  // 接收者pid
    std::atomic<uint32_t> senders_pid;  // 发送者pid，0默认有多个发送者
    std::atomic<uint32_t> flag; // 标志位
    // bit0：1允许发送，0不允许发送
    // bit1：1允许接收，0不允许接收
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
    std::atomic<uint32_t> receiver_pid;  // 接收者pid
    std::atomic<uint32_t> senders_pid;  // 发送者pid，0默认有多个发送者
    std::atomic<uint32_t> flag; // 标志位
    // bit0：1允许发送，0不允许发送
    // bit1：1允许接收，0不允许接收
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
    std::atomic<uint32_t> receiver_pid;  // 接收者pid
    std::atomic<uint32_t> senders_pid;  // 发送者pid，0默认有多个发送者
    std::atomic<uint32_t> flag; // 标志位
    // bit0：1允许发送，0不允许发送
    // bit1：1允许接收，0不允许接收
    LARGEDataSlot data[0];  // 柔性数组成员，指向共享内存数据区
} LARGERingQueueHeader;


/**
 * @brief 客户端状态信息结构体
**/
typedef struct {
    std::atomic<uint32_t> send_tail;  // 维护一个最后发送消息的尾部索引
    std::atomic<uint32_t> send_head;  // 维护一个最后发送消息的头部索引
    std::atomic<uint32_t> send_to_pid;  // 维护一个本进程最后发送消息给其他进程的pid
    std::atomic<uint32_t> local_pid;  // 维护一个本进程的pid
    std::atomic<bool> send_status;  // 维护一个最后发送消息的状态
}ClientStatusInfo;


/**
 * @brief 支持的数据区大小（字节）
 */
enum : uint32_t {
    SIZE_64B = 64,
    SIZE_1KB = 1024,
    SIZE_256KB = 256 * 1024,
};

class StreamShmCreator {
public:
    /**
     * @brief 构造函数
     * @param name 共享内存名称
     */
    StreamShmCreator(const std::string& name, uint32_t size = SIZE_64B, uint32_t slot_count = 1024);
    
    /**
     * @brief 析构函数，自动调用 close()
     */
    ~StreamShmCreator();

    StreamShmCreator(const StreamShmCreator&) = delete;
    StreamShmCreator& operator=(const StreamShmCreator&) = delete;

    /**
     * @brief 打开共享内存
     * @param create true=创建模式，false=仅打开模式
     * @return 成功返回true，失败返回false
     */
    bool open(bool create);
    
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
     * @return 满返回true，否则返回false
     */
    bool is_full();

    /**
     * @brief 获取共享内存名称
     * @return 共享内存名称
     */
    std::string get_shm_name(); 
    
    /**
     * @brief 设置客户端信息指针
     * @param client_info 客户端信息指针
     */
    void set_client_info(ClientStatusInfo* client_info);

    /**
     * @brief 设置标志位
     * @param flag 标志位
     */
    void set_flag(uint32_t flag);

private:
    /**
     * @brief 创建共享内存结构体
     */
    bool create_shm(bool create);
    
    /**
     * @brief 删除共享内存
     */
    void delete_shm();

    template<typename Header>
    int send_impl(Header* hdr, const std::vector<uint8_t>& msg);
    template<typename Header>
    uint32_t recv_impl(Header* hdr, std::vector<uint8_t>& buf);
    template<typename Header>
    bool is_empty_impl(Header* hdr) const;
    template<typename Header>
    bool is_full_impl(Header* hdr) const;
    template<typename Header>
    void set_flag_impl(Header* hdr, uint32_t flag);

private:
    std::string shm_name_;
    uint32_t slot_size_;
    uint32_t slot_count_;
    // 共享内存总大小
    uint32_t total_size_;
    int shm_fd_;
    void* shm_ptr_;
    bool is_owner_;
    ClientStatusInfo* client_info_;
};

template<typename Header>
int StreamShmCreator::send_impl(Header* hdr, const std::vector<uint8_t>& msg) {
    if ((hdr->flag.load(std::memory_order_acquire) & Define::BIT0) == 0) {
        return -1;
    }

    // 这里把数据写入到共享内存中
    uint32_t old_tail, new_tail;
    const uint32_t total_len = msg.size() + 4;
    if (slot_size_ == 0 || slot_count_ == 0) {
        return -1;
    }
    // 向上取整，计算需要多少个数据区
    const uint32_t slot_need = (total_len + slot_size_ - 1) / slot_size_;

    // CAS抢占队尾位置
    while (1) {
        old_tail = hdr->tail.load(std::memory_order_acquire);
        new_tail = (old_tail + slot_need) % slot_count_;

        // 检查空间是否足够
        uint32_t h = hdr->head.load(std::memory_order_acquire);
        uint32_t available = (h - old_tail - 1 + slot_count_) % slot_count_;
        if (available >= slot_need) {
            if (hdr->tail.compare_exchange_weak(old_tail, new_tail, std::memory_order_acquire, std::memory_order_relaxed)) {
                break;
            }
        } else {
            return -1;
        }
    }

    // 把数据写入到共享内存中，一个slot的写进去
    uint32_t t_msg_index = 0;
    uint32_t t_slice_id = 0;
    while (t_msg_index < msg.size()) {
        uint32_t t_len = slot_size_;

        // 第一个slice记录总长度和slice信息
        if (t_msg_index == 0){
            t_len -= 4;
            memcpy(hdr->data[old_tail].data, total_len - 4, 4);
            memcpy(hdr->data[old_tail].data + 4, msg.data(), (slot_need > 1) ? t_len : msg.size());
            hdr->data[old_tail].slice_id.store(t_slice_id, std::memory_order_release);
            hdr->data[old_tail].slice_count.store(slot_need, std::memory_order_release);
        } else {
            memcpy(hdr->data[old_tail].data, msg.data() + t_msg_index, (msg.size() - t_msg_index >= t_len) ? t_len : msg.size() - t_msg_index);
        }

        // 提交slice信息
        hdr->data[old_tail].commit.store(true, std::memory_order_release);
        old_tail = (old_tail + 1) % slot_count_;
        t_slice_id += 1;
        t_msg_index += t_len;
    }

    // 提交信号量
    sem_post(&hdr->sem);

    return msg.size();
}

template<typename Header>
uint32_t StreamShmCreator::recv_impl(Header* hdr, std::vector<uint8_t>& buf) {
    if ((hdr->flag.load(std::memory_order_acquire) & Define::BIT1) == 0) {
        return 0;
    }

    // 等待信号量
    sem_wait(&hdr->sem);

    // 不允许接收，退出逻辑需要清理
    if ((hdr->flag.load(std::memory_order_acquire) & Define::BIT1) == 0) {
        return 0;
    }

    uint32_t head, slice_count = 0, t_msg_index = 0;
    // 获取数据
    while (1) {
        head = hdr->head.load(std::memory_order_acquire);
        // 如果已经提交了标志位
        if (hdr->data[head].commit.load(std::memory_order_acquire)) {
            if (slice_count == 0) {
                slice_count = hdr->data[head].slice_count.load(std::memory_order_acquire);
                hdr->data[head].slice_id.store(0, std::memory_order_release);
                hdr->data[head].slice_count.store(0, std::memory_order_release);
                // 预设buf的总大小为第一个slice的data前四个字节组成的无符号数大小
                uint32_t total_len = 0;
                memcpy(&total_len, hdr->data[head].data, 4);
                if (total_len == 0) {
                    hdr->head.store(head + 1, std::memory_order_release);
                    hdr->data[head].commit.store(false, std::memory_order_release);
                    slice_count = 0;
                    continue;
                }
                buf.resize(total_len);
                memcpy(buf.data(), hdr->data[head].data + 4, slot_size_ - 4);
                t_msg_index += slot_size_ - 4;
            } else {
                memcpy(buf.data() + t_msg_index, hdr->data[head].data, slot_size_);
                t_msg_index += slot_size_;
            }
            hdr->data[head].commit.store(false, std::memory_order_release);
            if (slice_count == hdr->data[head].slice_id.load(std::memory_order_acquire)) {
                break;
            }
            head = (head + 1) % slot_count_;
        }
    }
    hdr->head.store(head, std::memory_order_release);
    return t_msg_index;
}

template<typename Header>
bool StreamShmCreator::is_empty_impl(Header* hdr) const {
    return hdr->head.load(std::memory_order_acquire) == hdr->tail.load(std::memory_order_acquire);
}

template<typename Header>
bool StreamShmCreator::is_full_impl(Header* hdr) const {
    return (hdr->tail.load(std::memory_order_acquire) + 1) % slot_count_ == hdr->head.load(std::memory_order_acquire);
}

template<typename Header>
void StreamShmCreator::set_flag_impl(Header* hdr, uint32_t flag) {
    hdr->flag.store(flag, std::memory_order_release);
}

} // namespace MulProcess
} // namespace IpcInterface