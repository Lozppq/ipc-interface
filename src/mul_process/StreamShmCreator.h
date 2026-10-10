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
#include <sched.h>
#include <semaphore.h>
#endif

namespace IpcInterface
{
namespace MulProcess
{

/**
 * @brief 支持的数据区大小（字节）
 * SMALL 单位是字节，MEDIUM 单位是 KB，LARGE 单位是 MB
 */
enum : uint32_t
{
    SIZE_64B = 64,
    SIZE_256B = 256,
    SIZE_1KB = 1024,
    SIZE_64KB = 64 * 1024,
    SIZE_256KB = 256 * 1024,
    SIZE_1MB = 1024 * 1024,
};

// 不同级别槽位的超时时间限制，单位微妙，不能设置太小，避免高优先级线程调度问题
enum : uint32_t
{
    TIMEOUT_64B = 100000,
    TIMEOUT_256B = 110000,
    TIMEOUT_1KB = 120000,
    TIMEOUT_64KB = 140000,
    TIMEOUT_256KB = 150000,
    TIMEOUT_1MB = 180000,
};

// 这个代表最大分片的数量，目前分片id是uint8_t类型，所以最大分片数量为255
constexpr uint32_t MAX_SLICE_COUNT = 255;

// 这个代表一个共享内存中，一个槽位可以被多少个读者读取
constexpr uint8_t MAX_READER_COUNT = 8;
constexpr uint8_t INVALID_READER_INDEX = 0xFF;

typedef struct
{
    std::atomic<uint8_t> m_slice_id;  // 切片id
    std::atomic<uint8_t> m_slice_count;  // 切片数量
    std::atomic<uint32_t> m_seq;  // 已提交序号，值为 produce_seq+1；0 表示未提交
    uint8_t m_data[SIZE_64B];
}SMALLDataSlot;

typedef struct
{
    std::atomic<uint8_t> m_slice_id;  // 切片id
    std::atomic<uint8_t> m_slice_count;  // 切片数量
    std::atomic<uint32_t> m_seq;  // 已提交序号，值为 produce_seq+1；0 表示未提交
    uint8_t m_data[SIZE_256B];
}SMALL256DataSlot;

typedef struct
{
    std::atomic<uint8_t> m_slice_id;  // 切片id
    std::atomic<uint8_t> m_slice_count;  // 切片数量
    std::atomic<uint32_t> m_seq;  // 已提交序号，值为 produce_seq+1；0 表示未提交
    uint8_t m_data[SIZE_1KB];
}MEDIUMDataSlot;

typedef struct
{
    std::atomic<uint8_t> m_slice_id;  // 切片id
    std::atomic<uint8_t> m_slice_count;  // 切片数量
    std::atomic<uint32_t> m_seq;  // 已提交序号，值为 produce_seq+1；0 表示未提交
    uint8_t m_data[SIZE_64KB];
}MEDIUM64DataSlot;

typedef struct
{
    std::atomic<uint8_t> m_slice_id;  // 切片id
    std::atomic<uint8_t> m_slice_count;  // 切片数量
    std::atomic<uint32_t> m_seq;  // 已提交序号，值为 produce_seq+1；0 表示未提交
    uint8_t m_data[SIZE_256KB];
}MEDIUM256DataSlot;

typedef struct
{
    std::atomic<uint8_t> m_slice_id;  // 切片id
    std::atomic<uint8_t> m_slice_count;  // 切片数量
    std::atomic<uint32_t> m_seq;  // 已提交序号，值为 produce_seq+1；0 表示未提交
    uint8_t m_data[SIZE_1MB];
}LARGEDataSlot;

/**
 * @brief 小数据环形队列结构体
**/
typedef struct
{
#if defined(__linux__)
    sem_t m_sem[MAX_READER_COUNT];           // 信号量，用于消费者阻塞等待
#endif
    std::atomic<uint32_t> m_reader_head[MAX_READER_COUNT]; // 各读者 consume 序号
    std::atomic<uint32_t> m_tail;  // 队尾序号（单调递增，物理下标 = seq % slot_count）
    std::atomic<uint32_t> m_slot_size; // 数据区大小
    std::atomic<uint32_t> m_slot_count; // 数据区数量
    std::atomic<uint32_t> m_flag; // 标志位
    // bit0：1允许发送，0不允许发送
    // bit1：1允许接收，0不允许接收
    std::atomic<uint8_t> m_reader_flag; // 已分配的读者槽位 bitmask
    SMALLDataSlot m_data[0];  // 柔性数组成员，指向共享内存数据区
} SMALLRingQueueHeader;

/**
 * @brief 256B 数据环形队列结构体
**/
typedef struct
{
#if defined(__linux__)
    sem_t m_sem[MAX_READER_COUNT];           // 信号量，用于消费者阻塞等待
#endif
    std::atomic<uint32_t> m_reader_head[MAX_READER_COUNT]; // 各读者 consume 序号
    std::atomic<uint32_t> m_tail;  // 队尾序号（单调递增，物理下标 = seq % slot_count）
    std::atomic<uint32_t> m_slot_size; // 数据区大小
    std::atomic<uint32_t> m_slot_count; // 数据区数量
    std::atomic<uint32_t> m_flag; // 标志位
    // bit0：1允许发送，0不允许发送
    // bit1：1允许接收，0不允许接收
    std::atomic<uint8_t> m_reader_flag; // 已分配的读者槽位 bitmask
    SMALL256DataSlot m_data[0];  // 柔性数组成员，指向共享内存数据区
} SMALL256RingQueueHeader;

/**
 * @brief 中数据环形队列结构体
**/
typedef struct
{
#if defined(__linux__)
    sem_t m_sem[MAX_READER_COUNT];           // 信号量，用于消费者阻塞等待
#endif
    std::atomic<uint32_t> m_reader_head[MAX_READER_COUNT]; // 各读者 consume 序号
    std::atomic<uint32_t> m_tail;  // 队尾序号（单调递增，物理下标 = seq % slot_count）
    std::atomic<uint32_t> m_slot_size; // 数据区大小
    std::atomic<uint32_t> m_slot_count; // 数据区数量
    std::atomic<uint32_t> m_flag; // 标志位
    // bit0：1允许发送，0不允许发送
    // bit1：1允许接收，0不允许接收
    std::atomic<uint8_t> m_reader_flag; // 已分配的读者槽位 bitmask
    MEDIUMDataSlot m_data[0];  // 柔性数组成员，指向共享内存数据区
} MEDIUMRingQueueHeader;

/**
 * @brief 64KB 数据环形队列结构体
**/
typedef struct
{
#if defined(__linux__)
    sem_t m_sem[MAX_READER_COUNT];           // 信号量，用于消费者阻塞等待
#endif
    std::atomic<uint32_t> m_reader_head[MAX_READER_COUNT]; // 各读者 consume 序号
    std::atomic<uint32_t> m_tail;  // 队尾序号（单调递增，物理下标 = seq % slot_count）
    std::atomic<uint32_t> m_slot_size; // 数据区大小
    std::atomic<uint32_t> m_slot_count; // 数据区数量
    std::atomic<uint32_t> m_flag; // 标志位
    // bit0：1允许发送，0不允许发送
    // bit1：1允许接收，0不允许接收
    std::atomic<uint8_t> m_reader_flag; // 已分配的读者槽位 bitmask
    MEDIUM64DataSlot m_data[0];  // 柔性数组成员，指向共享内存数据区
} MEDIUM64RingQueueHeader;

/**
 * @brief 256KB 数据环形队列结构体
**/
typedef struct
{
#if defined(__linux__)
    sem_t m_sem[MAX_READER_COUNT];           // 信号量，用于消费者阻塞等待
#endif
    std::atomic<uint32_t> m_reader_head[MAX_READER_COUNT]; // 各读者 consume 序号
    std::atomic<uint32_t> m_tail;  // 队尾序号（单调递增，物理下标 = seq % slot_count）
    std::atomic<uint32_t> m_slot_size; // 数据区大小
    std::atomic<uint32_t> m_slot_count; // 数据区数量
    std::atomic<uint32_t> m_flag; // 标志位
    // bit0：1允许发送，0不允许发送
    // bit1：1允许接收，0不允许接收
    std::atomic<uint8_t> m_reader_flag; // 已分配的读者槽位 bitmask
    MEDIUM256DataSlot m_data[0];  // 柔性数组成员，指向共享内存数据区
} MEDIUM256RingQueueHeader;

/**
 * @brief 1MB 数据环形队列结构体
**/
typedef struct
{
#if defined(__linux__)
    sem_t m_sem[MAX_READER_COUNT];           // 信号量，用于消费者阻塞等待
#endif
    std::atomic<uint32_t> m_reader_head[MAX_READER_COUNT]; // 各读者 consume 序号
    std::atomic<uint32_t> m_tail;  // 队尾序号（单调递增，物理下标 = seq % slot_count）
    std::atomic<uint32_t> m_slot_size; // 数据区大小
    std::atomic<uint32_t> m_slot_count; // 数据区数量
    std::atomic<uint32_t> m_flag; // 标志位
    // bit0：1允许发送，0不允许发送
    // bit1：1允许接收，0不允许接收
    std::atomic<uint8_t> m_reader_flag; // 已分配的读者槽位 bitmask
    LARGEDataSlot m_data[0];  // 柔性数组成员，指向共享内存数据区
} LARGERingQueueHeader;

class StreamShmCreator
{
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
     * @brief 获取正在发送的个数
     * @return 正在发送的个数
     */
    uint32_t get_sending_count();
    uint8_t get_reader_flag();
    uint8_t alloc_reader_slot();
    void free_reader_slot(uint8_t index);
    void drop_reader(uint8_t index);
    void set_reader_index(uint8_t index);

private:
    /**
     * @brief 创建共享内存结构体
     */
    bool create_shm(bool create);

    template<typename Header>
    uint32_t slowest_head(Header* hdr, uint8_t mask, uint32_t tail);
    template<typename Header>
    int send_impl(Header* hdr, std::shared_ptr<TagSendMessage> buf_msg);
    template<typename Header>
    uint32_t recv_impl(Header* hdr, std::shared_ptr<TagReceiveMessage> buf_msg);
    template<typename Header>
    void post_reader_sems(Header* hdr, uint8_t mask);

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
    std::atomic<uint32_t> m_sending_count{0}; // 正在发送的个数
    uint8_t m_reader_index{INVALID_READER_INDEX};
    uint32_t m_local_head{0};
};

template<typename Header>
uint32_t StreamShmCreator::slowest_head(Header* hdr, uint8_t mask, uint32_t tail)
{
    uint32_t slow = tail;
    uint32_t max_used = 0;
    for (uint8_t i = 0; i < MAX_READER_COUNT; i++)
    {
        if ((mask & static_cast<uint8_t>(1u << i)) == 0)
            continue;
        uint32_t rh = hdr->m_reader_head[i].load(std::memory_order_acquire);
        uint32_t used = tail - rh;
        if (used > max_used)
        {
            max_used = used;
            slow = rh;
        }
    }
    return slow;
}

template<typename Header>
int StreamShmCreator::send_impl(Header* hdr, std::shared_ptr<TagSendMessage> buf_msg)
{
    if (!hdr || !buf_msg || (hdr->m_flag.load(std::memory_order_acquire) & Define::BIT0) == 0)
        return -1;
    m_sending_count.fetch_add(1, std::memory_order_release);
    struct DecSending // 出函数自动减少正在发送的个数
    {
        std::atomic<uint32_t>& n;
        ~DecSending()
        {
            n.fetch_sub(1, std::memory_order_release);
        }
    } dec{m_sending_count};

    // 线格式: [4B payload_len][2B message_id][data...]，payload_len = 2 + data.size()
    uint32_t old_tail, new_tail;
    const uint32_t data_size = static_cast<uint32_t>(buf_msg->m_data.size());
    const uint32_t payload_len = 2u + data_size;
    const uint32_t total_len = 4u + payload_len;
    if (m_slot_size == 0 || m_slot_count == 0 || m_slot_size < 6)
        return -1;

    const uint32_t slot_need = (total_len + m_slot_size - 1) / m_slot_size;
    if (slot_need == 0 || slot_need > MAX_SLICE_COUNT)
        return -1;

    uint8_t reader_mask = 0;
    while (true)
    {
        reader_mask = hdr->m_reader_flag.load(std::memory_order_acquire);
        if (reader_mask == 0)
            return -1;
        old_tail = hdr->m_tail.load(std::memory_order_acquire);
        new_tail = old_tail + slot_need;
        uint32_t h = slowest_head(hdr, reader_mask, old_tail);
        uint32_t used = old_tail - h;
        if (used >= m_slot_count - 1 || m_slot_count - 1 - used < slot_need)
            return -1;
        if (hdr->m_tail.compare_exchange_weak(old_tail, new_tail,
                std::memory_order_acquire, std::memory_order_relaxed))
            break;
    }

    uint32_t seq = old_tail;
    uint32_t t_msg_index = 0;
    uint32_t t_slice_id = 0;
    bool first_slice = true;
    while (first_slice || t_msg_index < data_size)
    {
        auto& slot = hdr->m_data[seq % m_slot_count];
        uint32_t copied = 0;
        if (first_slice)
        {
            first_slice = false;
            Standard::Small_U32ToU8(payload_len, slot.m_data);
            Standard::Small_U16ToU8(buf_msg->m_message_id, slot.m_data + 4);
            uint32_t first_cap = m_slot_size - 6;
            copied = (data_size < first_cap) ? data_size : first_cap;
            if (copied > 0)
                memcpy(slot.m_data + 6, buf_msg->m_data.data(), copied);

            slot.m_slice_count.store(static_cast<uint8_t>(slot_need), std::memory_order_relaxed);
        }
        else
        {
            uint32_t remain = data_size - t_msg_index;
            copied = (remain < m_slot_size) ? remain : m_slot_size;
            memcpy(slot.m_data, buf_msg->m_data.data() + t_msg_index, copied);
        }
        slot.m_slice_id.store(static_cast<uint8_t>(t_slice_id), std::memory_order_relaxed);
        slot.m_seq.store(seq + 1, std::memory_order_release);
        seq += 1;
        t_slice_id += 1;
        t_msg_index += copied;
        if (copied == 0 && t_msg_index >= data_size)
            break;
    }
    post_reader_sems(hdr, reader_mask);
    return static_cast<int>(data_size);
}

template<typename Header>
void StreamShmCreator::post_reader_sems(Header* hdr, uint8_t mask)
{
#if defined(__linux__)
    for (uint8_t i = 0; i < MAX_READER_COUNT; i++)
    {
        if (mask & static_cast<uint8_t>(1u << i))
            sem_post(&hdr->m_sem[i]);
    }
#else
    (void)hdr;
    (void)mask;
#endif
}

template<typename Header>
uint32_t StreamShmCreator::recv_impl(Header* hdr, std::shared_ptr<TagReceiveMessage> buf_msg)
{
    if (!hdr || m_reader_index >= MAX_READER_COUNT || m_slot_count == 0
        || (hdr->m_flag.load(std::memory_order_acquire) & Define::BIT1) == 0)
        return 0;

#if defined(__linux__)
    sem_wait(&hdr->m_sem[m_reader_index]);
#endif

    if ((hdr->m_flag.load(std::memory_order_acquire) & Define::BIT1) == 0)
        return 0;

    uint32_t head = m_local_head, slice_count = 0, slices_done = 0, t_msg_index = 0, tail_last = ~0u;
    uint64_t start_time = 0;
    while (true)
    {
        uint32_t tail = hdr->m_tail.load(std::memory_order_acquire);
        if (head == tail)
            break;
        auto& slot = hdr->m_data[head % m_slot_count];
        if (slot.m_seq.load(std::memory_order_acquire) == head + 1)
        {
            const uint8_t slice_id = slot.m_slice_id.load(std::memory_order_acquire);
            if (slice_count == 0)
            {
                slice_count = slot.m_slice_count.load(std::memory_order_acquire);
                uint32_t total_len = Standard::Small_U8ToU32(slot.m_data);
                if (slice_id != 0 || slice_count == 0 || total_len < 2 || m_slot_size < 6)
                {
                    head += 1;
                    hdr->m_reader_head[m_reader_index].store(head, std::memory_order_release);
                    slice_count = 0;
                    continue;
                }
                const uint32_t payload_total = total_len - 2;
                buf_msg->m_data.resize(payload_total);
                buf_msg->m_message_id = Standard::Small_U8ToU16(slot.m_data + 4);
                uint32_t first_copy = payload_total < m_slot_size - 6 ? payload_total : m_slot_size - 6;
                memcpy(buf_msg->m_data.data(), slot.m_data + 6, first_copy);
                t_msg_index = first_copy;
            }
            else if (slice_id != slices_done)
            {
                buf_msg->m_data.clear();
                t_msg_index = 0;
                slices_done = 0;
                slice_count = 0;
                head += 1;
                hdr->m_reader_head[m_reader_index].store(head, std::memory_order_release);
                continue;
            }
            else
            {
                uint32_t remain = static_cast<uint32_t>(buf_msg->m_data.size()) - t_msg_index;
                uint32_t copy_len = (remain < m_slot_size) ? remain : m_slot_size;
                if (copy_len > 0)
                    memcpy(buf_msg->m_data.data() + t_msg_index, slot.m_data, copy_len);
                t_msg_index += copy_len;
            }
            slices_done++;
            head += 1;
            hdr->m_reader_head[m_reader_index].store(head, std::memory_order_release);
            if (slice_count > 0 && slices_done >= slice_count)
                break;
        }
        else
        {
            uint32_t not_commit_head = head, not_commit_end;
            if (tail - head > MAX_SLICE_COUNT)
                not_commit_end = head + MAX_SLICE_COUNT;
            else
                not_commit_end = tail;

            while (not_commit_head != not_commit_end)
            {
                if (hdr->m_data[not_commit_head % m_slot_count].m_seq.load(std::memory_order_acquire)
                    == not_commit_head + 1)
                    break;
                not_commit_head += 1;
            }

            if (tail_last != not_commit_head)
            {
                tail_last = not_commit_head;
                start_time = Standard::GetTimestamp();
            }
            else if (Standard::GetTimestamp() - start_time > m_slot_timeout)
            {
                LOG_ERROR("StreamShmCreator::recv_impl timeout, name=%s, head=%d, not_commit_head=%d, data_size=%zu",
                    m_shm_name.c_str(), head, not_commit_head, buf_msg->m_data.size());
                buf_msg->m_data.clear();
                t_msg_index = 0;
                slice_count = 0;
                slices_done = 0;
                head = not_commit_head;
                hdr->m_reader_head[m_reader_index].store(head, std::memory_order_release);
                break;
            }
#if defined(__linux__)
            sched_yield();
#endif
        }
    }
    if (slice_count > 0 && slices_done < slice_count)
    {
        buf_msg->m_data.clear();
        t_msg_index = 0;
    }
    m_local_head = head;
    hdr->m_reader_head[m_reader_index].store(head, std::memory_order_release);
    return t_msg_index;
}

} // namespace MulProcess
} // namespace IpcInterface
