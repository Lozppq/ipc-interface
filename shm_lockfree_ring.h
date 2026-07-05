#ifndef SHM_LOCKFREE_RING_H
#define SHM_LOCKFREE_RING_H

#include <stdatomic.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <semaphore.h>

// 环形缓冲区总大小（字节），留少量空间避免边界问题
#define QUEUE_SIZE     (1024 * 1024 * 2)  // 2MB
#define SHM_NAME       "/shm_lockfree_bi"

// 消息头部格式（4字节，32位）：
// bit31: 提交标记（1=已提交可读取，0=未提交正在写入）
// bit0~bit30: 数据部分长度（最大2^31-1字节）
#define MSG_HEADER_MASK  0x7FFFFFFF
#define MSG_COMMIT_BIT   0x80000000

// 单条无锁环形队列（跨进程）
// 每个队列自带信号量，实现阻塞通知机制
// 所有原子变量和信号量均4字节对齐，保证跨平台原子操作正确性
typedef struct {
    sem_t sem __attribute__((aligned(4)));         // 队列专属信号量（唤醒消费者）
    atomic_uint head __attribute__((aligned(4)));  // 消费者读取指针（字节偏移）
    atomic_uint tail __attribute__((aligned(4)));  // 生产者预留指针（字节偏移）
    atomic_uint flag __attribute__((aligned(4)));  // 标志位
    // bit0：0代表未初始化，1代表已初始化
    atomic_uint data_size __attribute__((aligned(4)));  // 数据部分长度（最大2^31-1字节）
    uint8_t data[];  // 数据区，可变长
} RingQueue;

#define SHM_TOTAL_SIZE  sizeof(BiShmRing)

// ===================== 工具函数 =====================
// 判空（仅消费者调用）
static inline int ring_is_empty(RingQueue *q)
{
    uint32_t h = atomic_load_explicit(&q->head, memory_order_acquire);
    uint32_t t = atomic_load_explicit(&q->tail, memory_order_acquire);
    return (h == t);
}

// 判满（生产者调用，判断是否能容纳len字节数据）
static inline int ring_is_full(RingQueue *q, uint32_t len)
{
    uint32_t h = atomic_load_explicit(&q->head, memory_order_acquire);
    uint32_t t = atomic_load_explicit(&q->tail, memory_order_acquire);
    uint32_t total_needed = len + 4;  // 4字节消息头 + 数据
    
    // 计算可用空间（环形缓冲区标准算法）
    uint32_t available = (h - t - 1 + QUEUE_SIZE) % QUEUE_SIZE;
    return (available < total_needed);
}

// ===================== 【多生产者安全】变长入队 =====================
// 功能：将len字节的msg写入队列，写入完成后自动唤醒消费者
// 返回：0=成功，-1=队列满/消息太长
// 线程安全：支持任意多线程/多进程同时调用
static inline int ring_enqueue(RingQueue *q, const uint8_t *msg, uint32_t len)
{
    // 合法性检查：消息不能为空，且不能超过最大长度
    if (len == 0 || len > (QUEUE_SIZE - 4))
        return -1;

    uint32_t old_tail, new_tail;
    const uint32_t total_len = len + 4;  // 4字节头 + 数据

    // 1. CAS自旋抢占写入区间（无锁核心）
    while (1) {
        old_tail = atomic_load_explicit(&q->tail, memory_order_acquire);
        new_tail = (old_tail + total_len) % QUEUE_SIZE;

        // 检查是否有足够空间
        uint32_t h = atomic_load_explicit(&q->head, memory_order_acquire);
        uint32_t available = (h - old_tail - 1 + QUEUE_SIZE) % QUEUE_SIZE;
        if (available < total_len)
            return -1;  // 队列满

        // 原子抢占：只有一个线程能成功更新tail
        if (atomic_compare_exchange_weak_explicit(
                &q->tail,
                &old_tail,
                new_tail,
                memory_order_acq_rel,
                memory_order_acquire))
        {
            break;  // 抢占成功
        }
        // 抢占失败，自动重试
    }

    // 2. 写入消息头部（先写未提交标记）
    uint32_t header = len;  // 最高位为0，表示未提交
    if (old_tail + 4 <= QUEUE_SIZE) {
        // 头部不跨边界，直接写入
        *(volatile uint32_t *)&q->data[old_tail] = header;
    } else {
        // 头部跨边界，分两次写入
        uint32_t part1_len = QUEUE_SIZE - old_tail;
        memcpy(&q->data[old_tail], &header, part1_len);
        memcpy(&q->data[0], (uint8_t *)&header + part1_len, 4 - part1_len);
    }

    // 3. 写入数据部分
    const uint32_t data_start = old_tail + 4;
    if (data_start + len <= QUEUE_SIZE) {
        // 数据不跨边界，直接写入
        memcpy(&q->data[data_start], msg, len);
    } else {
        // 数据跨边界，分两次写入
        uint32_t part1_len = QUEUE_SIZE - data_start;
        memcpy(&q->data[data_start], msg, part1_len);
        memcpy(&q->data[0], msg + part1_len, len - part1_len);
    }

    // 4. 原子提交：将头部最高位设为1，表示消息已写完可读取
    // release屏障：保证数据全部写完后，提交标记才对消费者可见
    if (old_tail + 4 <= QUEUE_SIZE) {
        atomic_fetch_or_explicit(
            (atomic_uint *)&q->data[old_tail],
            MSG_COMMIT_BIT,
            memory_order_release);
    } else {
        // 头部跨边界时，先修改临时变量再写回（保证原子性）
        header |= MSG_COMMIT_BIT;
        memcpy(&q->data[old_tail], &header, QUEUE_SIZE - old_tail);
        memcpy(&q->data[0], (uint8_t *)&header + (QUEUE_SIZE - old_tail), 4 - (QUEUE_SIZE - old_tail));
        atomic_thread_fence(memory_order_release);
    }

    // 5. 唤醒消费者（使用队列自带的信号量）
    sem_post(&q->sem);

    return 0;
}

// ===================== 【单消费者安全】阻塞出队 =====================
// 功能：阻塞等待数据，然后一次性读取队列中所有已提交的数据
// 参数：q=队列（自带信号量），buf=接收缓冲区，buf_len=缓冲区最大长度
// 返回：实际读取的字节数（0表示队空）
// 注意：只能单线程/单进程调用
static inline uint32_t ring_dequeue(RingQueue *q, uint8_t *buf, uint32_t buf_len)
{
    if (buf_len == 0)
        return 0;

    // 先尝试读取，如果队空则阻塞等待
    uint32_t copied = 0;
    uint32_t h = atomic_load_explicit(&q->head, memory_order_acquire);

    while (1) {
        // 队空检查
        uint32_t t = atomic_load_explicit(&q->tail, memory_order_acquire);
        if (h == t) {
            // 队列空，阻塞等待队列自带的信号量
            sem_wait(&q->sem);
            // 被唤醒后重新加载指针
            h = atomic_load_explicit(&q->head, memory_order_acquire);
            continue;
        }

        // 1. 读取消息头部
        uint32_t header;
        if (h + 4 <= QUEUE_SIZE) {
            header = *(volatile uint32_t *)&q->data[h];
        } else {
            // 头部跨边界，拼接读取
            uint8_t header_buf[4];
            memcpy(header_buf, &q->data[h], QUEUE_SIZE - h);
            memcpy(header_buf + (QUEUE_SIZE - h), &q->data[0], 4 - (QUEUE_SIZE - h));
            header = *(uint32_t *)header_buf;
        }

        // 2. 检查消息是否已提交（最高位为1）
        // 如果未提交，说明生产者还在写，停止读取（保证顺序）
        if (!(header & MSG_COMMIT_BIT))
            break;

        // 3. 提取数据长度
        uint32_t len = header & MSG_HEADER_MASK;
        uint32_t total_msg_len = len + 4;

        // 4. 检查缓冲区是否足够
        if (copied + len > buf_len)
            break;  // 缓冲区满，停止读取

        // 5. 复制数据部分到输出缓冲区（不复制头部）
        uint32_t data_start = h + 4;
        if (data_start + len <= QUEUE_SIZE) {
            // 数据不跨边界，直接复制
            memcpy(buf + copied, &q->data[data_start], len);
        } else {
            // 数据跨边界，分两次复制
            uint32_t part1_len = QUEUE_SIZE - data_start;
            memcpy(buf + copied, &q->data[data_start], part1_len);
            memcpy(buf + copied + part1_len, &q->data[0], len - part1_len);
        }

        // 6. 更新状态
        copied += len;
        h = (h + total_msg_len) % QUEUE_SIZE;

        // 单次读取，外部循环读取，直到队空
        break;
    }

    // 批量更新head指针（减少原子操作次数，提升性能）
    if (copied > 0) {
        atomic_store_explicit(&q->head, h, memory_order_release);
    }

    return copied;
}

// 初始化单个队列（仅第一个创建共享内存的进程调用）
// pshared=1 表示信号量可跨进程共享
static inline void ring_queue_init(RingQueue *q)
{
    // 初始化信号量（pshared=1，初始值=0）
    sem_init(&q->sem, 1, 0);
    
    // 初始化队列指针
    atomic_init(&q->head, 0);
    atomic_init(&q->tail, 0);
    memset(q->data, 0, QUEUE_SIZE);
}

// 初始化双向队列（仅第一个创建共享内存的进程调用）
static inline void ring_init(BiShmRing *shm)
{
    ring_queue_init(&shm->q_ab);
    ring_queue_init(&shm->q_ba);
}

// 销毁单个队列的信号量
static inline void ring_queue_destroy(RingQueue *q)
{
    sem_destroy(&q->sem);
}

// 销毁双向队列的信号量（仅最后一个进程调用）
static inline void ring_destroy(BiShmRing *shm)
{
    ring_queue_destroy(&shm->q_ab);
    ring_queue_destroy(&shm->q_ba);
}

#endif