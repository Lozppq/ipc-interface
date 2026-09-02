#pragma once

#include <atomic>
#include <cstdint>
#include <cstring>
#include <memory>
#include <vector>
#include <string>
#include "../log/Log_Print.h"
#include "../define/Common.h"
#include "../standard/api.h"
#include "TagMessage.h"
#if defined(__linux__)
#include <semaphore.h>
#endif

namespace IpcInterface {
namespace MulProcess {
#define SMALL_DATA_SLOT_SIZE 64
#define MEDIUM_DATA_SLOT_SIZE 1024
#define LARGE_DATA_SLOT_SIZE (1024 * 256)

typedef struct {
    std::atomic<uint8_t> m_slice_id;  // 切片id
    std::atomic<uint8_t> m_slice_count;  // 切片数量
    std::atomic<uint8_t> m_commit;  // 提交标志位
    std::atomic<uint8_t> m_reader_count;  // 读者数量
    uint8_t m_data[SMALL_DATA_SLOT_SIZE];
}SMALLDataSlot;

typedef struct {
    std::atomic<uint8_t> m_slice_id;  // 切片id
    std::atomic<uint8_t> m_slice_count;  // 切片数量
    std::atomic<uint8_t> m_commit;  // 提交标志位
    std::atomic<uint8_t> m_reader_count;  // 读者数量
    uint8_t m_data[MEDIUM_DATA_SLOT_SIZE];
}MEDIUMDataSlot;

typedef struct {
    std::atomic<uint8_t> m_slice_id;  // 切片id
    std::atomic<uint8_t> m_slice_count;  // 切片数量
    std::atomic<uint8_t> m_commit;  // 提交标志位
    std::atomic<uint8_t> m_reader_count;  // 读者数量
    uint8_t m_data[LARGE_DATA_SLOT_SIZE];
}LARGEDataSlot;


/**
 * @brief 小数据环形队列结构体
**/
typedef struct {
#if defined(__linux__)
    sem_t m_sem;           // 信号量，用于消费者阻塞等待
#endif
    std::atomic<uint32_t> m_head;  // 队头指针
    std::atomic<uint32_t> m_tail;  // 队尾指针
    std::atomic<uint32_t> m_slot_size; // 数据区大小
    std::atomic<uint32_t> m_slot_count; // 数据区数量
    std::atomic<uint32_t> m_flag; // 标志位
    // bit0：1允许发送，0不允许发送
    // bit1：1允许接收，0不允许接收
    SMALLDataSlot m_data[0];  // 柔性数组成员，指向共享内存数据区
} SMALLRingQueueHeader;

/**
 * @brief 中数据环形队列结构体
**/
typedef struct {
#if defined(__linux__)
    sem_t m_sem;           // 信号量，用于消费者阻塞等待
#endif
    std::atomic<uint32_t> m_head;  // 队头指针
    std::atomic<uint32_t> m_tail;  // 队尾指针
    std::atomic<uint32_t> m_slot_size; // 数据区大小
    std::atomic<uint32_t> m_slot_count; // 数据区数量
    std::atomic<uint32_t> m_flag; // 标志位
    // bit0：1允许发送，0不允许发送
    // bit1：1允许接收，0不允许接收
    MEDIUMDataSlot m_data[0];  // 柔性数组成员，指向共享内存数据区
} MEDIUMRingQueueHeader;

/**
 * @brief 大数据环形队列结构体
**/
typedef struct {
#if defined(__linux__)
    sem_t m_sem;           // 信号量，用于消费者阻塞等待
#endif
    std::atomic<uint32_t> m_head;  // 队头指针
    std::atomic<uint32_t> m_tail;  // 队尾指针
    std::atomic<uint32_t> m_slot_size; // 数据区大小
    std::atomic<uint32_t> m_slot_count; // 数据区数量
    std::atomic<uint32_t> m_flag; // 标志位
    // bit0：1允许发送，0不允许发送
    // bit1：1允许接收，0不允许接收
    LARGEDataSlot m_data[0];  // 柔性数组成员，指向共享内存数据区
} LARGERingQueueHeader;


enum{
    COMMIT_FALSE = 0,
    COMMIT_TRUE = 1,
};


/**
 * @brief 支持的数据区大小（字节）
 */
enum : uint32_t {
    SIZE_64B = 64,
    SIZE_1KB = 1024,
    SIZE_256KB = 256 * 1024,
};

// 不同级别槽位的超时时间限制，单位微妙，不能设置太小，避免高优先级线程调度问题
enum : uint32_t {
    TIMEOUT_64B = 10000,
    TIMEOUT_1KB = 20000,
    TIMEOUT_256KB = 50000,
};

// 这个代表最大分片的数量，目前分片id是uint8_t类型，所以最大分片数量为255
constexpr uint32_t MAX_SLICE_COUNT = 255;

class StreamShmCreator {
public:
    /**
     * @brief 构造函数
     * @param name 共享内存名称
     */
    StreamShmCreator(const std::string& name, uint32_t slot_size = SIZE_64B, uint32_t slot_count = 1024);
    
    /**
     * @brief 析构函数，自动调用 Close()
     */
    ~StreamShmCreator();

    StreamShmCreator(const StreamShmCreator&) = delete;
    StreamShmCreator& operator=(const StreamShmCreator&) = delete;

    /**
     * @brief 打开共享内存
     * @param create true=创建模式，false=仅打开模式
     * @return 成功返回true，失败返回false
     */
    bool Open(bool create);
    
    /**
     * @brief 关闭共享内存，释放资源
     */
    void Close();

    /**
     * @brief 删除共享内存
     */
    void delete_shm();
    
    /**
     * @brief 检查是否有效
     * @return 有效返回true，否则返回false
     */
    bool valid() const;

    /**
     * @brief 发送消息
     * @param buf_msg 消息数据
     * @return 成功返回0，失败返回-1
     */
    int send(std::shared_ptr<TagSendMessage> buf_msg);
    
    /**
     * @brief 接收消息，按消息实际长度调整 buf_msg 并写入数据
     * @param buf_msg 接收缓冲区，内部会 resize 到消息长度
     * @return 接收字节数，失败返回0
     */
    uint32_t recv(std::shared_ptr<TagReceiveMessage> buf_msg);

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
     * @brief 设置标志位
     * @param flag 标志位
     */
    void set_flag(uint32_t flag);

    /**
     * @brief 清除接收允许位并唤醒阻塞在 recv 上的 sem_wait
     */
    void wakeup_recv();

    /**
     * @brief 获取当前时间戳
     * @return 相对开机的单调时钟微秒数（steady_clock / CLOCK_MONOTONIC）
    */
    uint64_t get_timestamp();

private:
    /**
     * @brief 创建共享内存结构体
     */
    bool create_shm(bool create);

    template<typename Header>
    int send_impl(Header* hdr, std::shared_ptr<TagSendMessage> buf_msg);
    template<typename Header>
    uint32_t recv_impl(Header* hdr, std::shared_ptr<TagReceiveMessage> buf_msg);

private:
    std::string m_shm_name;
    uint32_t m_slot_size;
    uint32_t m_slot_count;
    // 共享内存总大小
    uint32_t m_total_size;
    int m_shm_fd;
    void* m_shm_ptr;
    bool m_is_owner;
    uint64_t m_slot_timeout;
};

template<typename Header>
int StreamShmCreator::send_impl(Header* hdr, std::shared_ptr<TagSendMessage> buf_msg) {
    if (!hdr || !buf_msg || (hdr->m_flag.load(std::memory_order_acquire) & Define::BIT0) == 0) {
        return -1;
    }

    // 线格式: [4B payload_len][2B message_id][data...]，payload_len = 2 + data.size()
    uint32_t old_tail, new_tail;
    const uint32_t data_size = static_cast<uint32_t>(buf_msg->m_data.size());
    const uint32_t payload_len = 2u + data_size;
    const uint32_t total_len = 4u + payload_len;
    if (m_slot_size == 0 || m_slot_count == 0 || m_slot_size < 6) {
        return -1;
    }
    const uint32_t slot_need = (total_len + m_slot_size - 1) / m_slot_size;

    while (1) {
        old_tail = hdr->m_tail.load(std::memory_order_acquire);
        new_tail = (old_tail + slot_need) % m_slot_count;

        uint32_t h = hdr->m_head.load(std::memory_order_acquire);
        uint32_t available = (h - old_tail - 1 + m_slot_count) % m_slot_count;
        if (available >= slot_need) {
            if (hdr->m_tail.compare_exchange_weak(old_tail, new_tail, std::memory_order_acquire, std::memory_order_relaxed)) {
                break;
            }
        } else {
            return -1;
        }
    }

    uint32_t t_msg_index = 0;
    uint32_t t_slice_id = 0;
    bool first_slice = true;
    while (first_slice || t_msg_index < data_size) {
        uint32_t copied = 0;
        if (first_slice) {
            first_slice = false;
            Standard::Small_U32ToU8(payload_len, hdr->m_data[old_tail].m_data);
            Standard::Small_U16ToU8(buf_msg->m_message_id, hdr->m_data[old_tail].m_data + 4);
            uint32_t first_cap = m_slot_size - 6;
            copied = (data_size < first_cap) ? data_size : first_cap;
            if (copied > 0) {
                memcpy(hdr->m_data[old_tail].m_data + 6, buf_msg->m_data.data(), copied);
            }
            hdr->m_data[old_tail].m_slice_id.store(t_slice_id, std::memory_order_release);
            hdr->m_data[old_tail].m_slice_count.store(slot_need, std::memory_order_release);
        } else {
            uint32_t remain = data_size - t_msg_index;
            copied = (remain < m_slot_size) ? remain : m_slot_size;
            memcpy(hdr->m_data[old_tail].m_data, buf_msg->m_data.data() + t_msg_index, copied);
        }

        hdr->m_data[old_tail].m_commit.store(COMMIT_TRUE, std::memory_order_release);
        old_tail = (old_tail + 1) % m_slot_count;
        t_slice_id += 1;
        t_msg_index += copied;
        if (copied == 0 && t_msg_index >= data_size) {
            break;
        }
    }

#if defined(__linux__)
    sem_post(&hdr->m_sem);
#endif
    return static_cast<int>(data_size);
}

template<typename Header>
uint32_t StreamShmCreator::recv_impl(Header* hdr, std::shared_ptr<TagReceiveMessage> buf_msg) {
    if (!hdr || (hdr->m_flag.load(std::memory_order_acquire) & Define::BIT1) == 0) {
        return 0;
    }

#if defined(__linux__)
    sem_wait(&hdr->m_sem);
#endif

    // 不允许接收，退出逻辑需要清理
    if ((hdr->m_flag.load(std::memory_order_acquire) & Define::BIT1) == 0) {
        return 0;
    }

    // 用于计算槽接收超时
    uint64_t start_time = get_timestamp();

    uint32_t head, slice_count = 0, slices_done = 0, t_msg_index = 0, tail_last = 0;
    head = hdr->m_head.load(std::memory_order_acquire);
    // 获取数据
    while (1) {
        // 如果已经提交了标志位
        if (hdr->m_data[head].m_commit.load(std::memory_order_acquire) == COMMIT_TRUE) {
            start_time = get_timestamp();
            if (slice_count == 0) {
                slice_count = hdr->m_data[head].m_slice_count.load(std::memory_order_acquire);
                hdr->m_data[head].m_slice_id.store(0, std::memory_order_release);
                hdr->m_data[head].m_slice_count.store(0, std::memory_order_release);
                if (slice_count == 0) {
                    hdr->m_data[head].m_commit.store(COMMIT_FALSE, std::memory_order_release);
                    head = (head + 1) % m_slot_count;
                    continue;
                }
                // 预设buf的总大小为第一个slice的data前四个字节组成的无符号数大小
                uint32_t total_len = Standard::Small_U8ToU32(hdr->m_data[head].m_data);
                if (total_len == 0) {
                    hdr->m_data[head].m_commit.store(COMMIT_FALSE, std::memory_order_release);
                    head = (head + 1) % m_slot_count;
                    slice_count = 0;
                    continue;
                }
                if (total_len < 2 || m_slot_size < 6) {
                    hdr->m_data[head].m_commit.store(COMMIT_FALSE, std::memory_order_release);
                    head = (head + 1) % m_slot_count;
                    slice_count = 0;
                    continue;
                }
                const uint32_t payload_total = total_len - 2;
                buf_msg->m_data.resize(payload_total);
                buf_msg->m_message_id = Standard::Small_U8ToU16(hdr->m_data[head].m_data + 4);
                uint32_t first_copy = payload_total;
                if (first_copy > m_slot_size - 6) {
                    first_copy = m_slot_size - 6;
                }
                if (first_copy > 0) {
                    memcpy(buf_msg->m_data.data(), hdr->m_data[head].m_data + 6, first_copy);
                }
                t_msg_index = first_copy;
            } else {
                uint32_t remain = static_cast<uint32_t>(buf_msg->m_data.size()) - t_msg_index;
                uint32_t copy_len = (remain < m_slot_size) ? remain : m_slot_size;
                if (copy_len > 0) {
                    memcpy(buf_msg->m_data.data() + t_msg_index, hdr->m_data[head].m_data, copy_len);
                }
                t_msg_index += copy_len;
            }
            hdr->m_data[head].m_commit.store(COMMIT_FALSE, std::memory_order_release);
            slices_done++;
            head = (head + 1) % m_slot_count;
            // 已收齐首片声明的 slice_count 片则结束（勿用被清零的 slice_id 判断）
            if (slice_count > 0 && slices_done >= slice_count) {
                break;
            }
        } else {
            // 启发式探索：这里需要计算，如果等待当前槽位到固定tail超时，则直接将当前槽位数据丢弃再break
            uint32_t tail = hdr->m_tail.load(std::memory_order_acquire);
            uint32_t not_commit_head = head, not_commit_tail;
            if ((tail - head + m_slot_count) % m_slot_count > MAX_SLICE_COUNT){
                not_commit_tail = (head + MAX_SLICE_COUNT) % m_slot_count;
            } else {
                not_commit_tail = tail;
            }

            // 计算从当前未commit到commit的最大槽位
            while (not_commit_head != not_commit_tail){
                if (hdr->m_data[not_commit_head].m_commit.load(std::memory_order_acquire) == COMMIT_TRUE){
                    break;
                } else {
                    not_commit_head = (not_commit_head + 1) % m_slot_count;
                }
            }

            if (tail_last != not_commit_head){
                tail_last = not_commit_head;
                start_time = get_timestamp();
            } else if (get_timestamp() - start_time > m_slot_timeout){ // 进入这里说明找到了一直未提交的tail，进入超时处理
                LOG_ERROR("StreamShmCreator::recv_impl timeout,name=%s,head=%d,not_commit_head=%d", m_shm_name.c_str(), head, not_commit_head);
                buf_msg->m_data.clear();
                t_msg_index = 0;
                head = not_commit_head;
                break;
            }
        }
    }
    hdr->m_head.store(head, std::memory_order_release);
    return t_msg_index;
}

} // namespace MulProcess
} // namespace IpcInterface