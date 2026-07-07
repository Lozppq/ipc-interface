/**
 * @file ShmManager.cpp
 * @brief 共享内存管理器实现
 * @details 实现共享内存的创建、打开、映射、队列操作等核心功能。
 * 使用 CAS 原子操作保证多生产者线程安全，消息格式为 [4字节长度+数据]，
 * 通过提交标志位确保数据写入完成后消费者才能读取，支持跨进程信号量同步。
 */

#include "ShmManager.h"
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <unistd.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <semaphore.h>
#include <chrono>

ShmManager::ShmManager(const char *name)
    : shm_fd_(-1), q_(nullptr), owner_(false), queue_size_(0) {
    snprintf(shm_name_, sizeof(shm_name_), "%s", name);
}

void ShmManager::failOpen() {
    if (q_ && q_ != MAP_FAILED) {
        uint32_t total_size = queue_size_
            ? sizeof(RingQueueHeader) + queue_size_
            : sizeof(RingQueueHeader);
        munmap(q_, total_size);
    }
    q_ = nullptr;
    if (shm_fd_ >= 0) {
        ::close(shm_fd_);
        shm_fd_ = -1;
    }
    queue_size_ = 0;
}

bool ShmManager::open(QueueSize size, bool create) {
    stop_recv_ = false;
    owner_ = create;
    queue_size_ = static_cast<uint32_t>(size);

    if (create) {
        // 创建模式：尝试创建新的共享内存
        shm_fd_ = shm_open(shm_name_, O_RDWR | O_CREAT | O_EXCL, 0666);
        if (shm_fd_ >= 0) {
            // 新创建成功，设置大小并映射
            uint32_t total_size = sizeof(RingQueueHeader) + queue_size_;
            ftruncate(shm_fd_, total_size);
            q_ = (RingQueueHeader *)mmap(nullptr, total_size, PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd_, 0);
            if (q_ != MAP_FAILED) {
                ring_init(q_, queue_size_);
                return true;
            }
        } else {
            // 共享内存已存在，仅打开并重置
            shm_fd_ = shm_open(shm_name_, O_RDWR, 0666);
            if (shm_fd_ >= 0) {
                q_ = (RingQueueHeader *)mmap(nullptr, sizeof(RingQueueHeader), PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd_, 0);
                if (q_ != MAP_FAILED) {
                    queue_size_ = q_->data_size.load(std::memory_order_acquire);
                    munmap(q_, sizeof(RingQueueHeader));
                    if (queue_size_ == 0) {
                        failOpen();
                        return false;
                    }
                    uint32_t total_size = sizeof(RingQueueHeader) + queue_size_;
                    q_ = (RingQueueHeader *)mmap(nullptr, total_size, PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd_, 0);
                    if (q_ != MAP_FAILED) {
                        ring_reset(q_, queue_size_);
                        return true;
                    }
                    q_ = nullptr;
                }
            }
        }
    } else {
        // 仅打开模式：连接已存在的共享内存
        shm_fd_ = shm_open(shm_name_, O_RDWR, 0666);
        if (shm_fd_ >= 0) {
            q_ = (RingQueueHeader *)mmap(nullptr, sizeof(RingQueueHeader), PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd_, 0);
            if (q_ != MAP_FAILED) {
                queue_size_ = q_->data_size.load(std::memory_order_acquire);
                munmap(q_, sizeof(RingQueueHeader));
                if (queue_size_ == 0) {
                    failOpen();
                    return false;
                }
                uint32_t total_size = sizeof(RingQueueHeader) + queue_size_;
                q_ = (RingQueueHeader *)mmap(nullptr, total_size, PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd_, 0);
                if (q_ != MAP_FAILED) {
                    return true;
                }
                q_ = nullptr;
            }
        }
    }
    failOpen();

    return false;
}

void ShmManager::close() {
    if (q_ && q_ != MAP_FAILED) {
        // 创建方close清空标志
        if (owner_) {
            q_->flag.store(0, std::memory_order_relaxed);
        }
        uint32_t total_size = sizeof(RingQueueHeader) + queue_size_;
        munmap(q_, total_size);
        q_ = nullptr;
    }
    if (shm_fd_ >= 0) {
        ::close(shm_fd_);
        shm_fd_ = -1;
    }
    // 仅创建方执行 unlink，确保所有连接断开后共享内存被销毁
    if (owner_) {
        shm_unlink(shm_name_);
        owner_ = false;
    }
    queue_size_ = 0;
    stop_recv_ = false;
}

ShmManager::~ShmManager() {
    close();
}

bool ShmManager::valid() const {
    return q_ && q_ != MAP_FAILED;
}

int ShmManager::send(const std::vector<uint8_t>& msg) {
    if (!valid() || msg.empty()) {
        return -1;
    }
    return ring_enqueue(q_, queue_size_, msg.data(), static_cast<uint32_t>(msg.size()));
}

uint32_t ShmManager::recv(std::vector<uint8_t>& buf) {
    if (!valid()) {
        return 0;
    }
    return ring_dequeue(q_, queue_size_, buf);
}

bool ShmManager::is_empty() {
    return ring_is_empty(q_);
}

void ShmManager::wake() {
    if (valid()) {
        sem_post(&q_->sem);
    }
}

void ShmManager::stopRecv() {
    stop_recv_ = true;
    wake();
}

bool ShmManager::is_full(uint32_t len) {
    return ring_is_full(q_, queue_size_, len);
}

bool ShmManager::ring_is_empty(RingQueueHeader *q) {
    uint32_t h = q->head.load(std::memory_order_acquire);
    uint32_t t = q->tail.load(std::memory_order_acquire);
    return (h == t);
}

bool ShmManager::ring_is_full(RingQueueHeader *q, uint32_t queue_size, uint32_t len) {
    uint32_t h = q->head.load(std::memory_order_acquire);
    uint32_t t = q->tail.load(std::memory_order_acquire);
    uint32_t total_needed = len + 4; // 4字节长度头
    uint32_t available = (h - t - 1 + queue_size) % queue_size;
    return (available < total_needed);
}

int ShmManager::ring_enqueue(RingQueueHeader *q, uint32_t queue_size, const uint8_t *msg, uint32_t len) {
    // 参数校验和写入权限检查
    if (len == 0 || len > (queue_size - 4) || 
    (q->flag.load(std::memory_order_acquire) & Flag::FLAG_ALLOW_WRITE) == 0)
        return -1;

    uint32_t old_tail, new_tail;
    const uint32_t total_len = len + 4;

    // CAS抢占队尾位置
    while (1) {
        old_tail = q->tail.load(std::memory_order_acquire);
        new_tail = (old_tail + total_len) % queue_size;

        // 检查空间是否足够
        uint32_t h = q->head.load(std::memory_order_acquire);
        uint32_t available = (h - old_tail - 1 + queue_size) % queue_size;
        if (available < total_len)
            return -1;

        // CAS成功则跳出循环
        if (q->tail.compare_exchange_weak(old_tail, new_tail,
                std::memory_order_acq_rel, std::memory_order_acquire)){
            break;
        }
    }

    // 写入长度头（不设置提交标志）
    uint32_t header = len;
    if (old_tail + 4 <= queue_size) {
        *(volatile uint32_t *)&q->data[old_tail] = header;
    } else {
        // 环形跨越边界处理
        uint32_t part1_len = queue_size - old_tail;
        memcpy(&q->data[old_tail], &header, part1_len);
        memcpy(&q->data[0], (uint8_t *)&header + part1_len, 4 - part1_len);
    }

    // 写入数据
    const uint32_t data_start = old_tail + 4;
    if (data_start + len <= queue_size) {
        memcpy(&q->data[data_start], msg, len);
    } else {
        uint32_t part1_len = queue_size - data_start;
        memcpy(&q->data[data_start], msg, part1_len);
        memcpy(&q->data[0], msg + part1_len, len - part1_len);
    }

    // 设置提交标志，通知消费者数据已写入完成
    if (old_tail + 4 <= queue_size) {
        std::atomic<uint32_t> *header_ptr = (std::atomic<uint32_t> *)&q->data[old_tail];
        header_ptr->fetch_or(MSG_COMMIT_BIT, std::memory_order_release);
    } else {
        header |= MSG_COMMIT_BIT;
        memcpy(&q->data[old_tail], &header, queue_size - old_tail);
        memcpy(&q->data[0], (uint8_t *)&header + (queue_size - old_tail), 4 - (queue_size - old_tail));
        std::atomic_thread_fence(std::memory_order_release);
    }

    // 唤醒消费者
    sem_post(&q->sem);
    return 0;
}

uint32_t ShmManager::ring_dequeue(RingQueueHeader *q, uint32_t queue_size, std::vector<uint8_t>& buf) {
    sem_wait(&q->sem);
    if (stop_recv_) {
        return 0;
    }
    // 数据未提交，继续等待
    auto now = std::chrono::steady_clock::now();
    while (1) {
        uint32_t h = q->head.load(std::memory_order_acquire);
        // 读取长度头
        uint32_t header = 0;
        if (h + 4 <= queue_size) {
            header = *(volatile uint32_t *)&q->data[h];
        } else {
            uint8_t header_buf[4];
            memcpy(header_buf, &q->data[h], queue_size - h);
            memcpy(header_buf + (queue_size - h), &q->data[0], 4 - (queue_size - h));
            header = *(uint32_t *)header_buf;
        }

        // 提取长度
        uint32_t len = header & MSG_HEADER_MASK;
        uint32_t total_msg_len = len + 4;

        if (!(header & MSG_COMMIT_BIT)) {
            if (std::chrono::steady_clock::now() - now < std::chrono::milliseconds(100))
            {
                usleep(10);
                continue;
            }
            // 清理数据
            q->head.store((h + total_msg_len) % queue_size, std::memory_order_release);
            return 0;
        }

        if (len == 0 || len > queue_size - 4) {
            h = (h + 4) % queue_size;
            q->head.store(h, std::memory_order_release);
            return 0;
        }

        buf.resize(len);

        // 读取数据
        uint32_t data_start = h + 4;
        if (data_start + len <= queue_size) {
            memcpy(buf.data(), &q->data[data_start], len);
        } else {
            uint32_t part1_len = queue_size - data_start;
            memcpy(buf.data(), &q->data[data_start], part1_len);
            memcpy(buf.data() + part1_len, &q->data[0], len - part1_len);
        }

        h = (h + total_msg_len) % queue_size;
        q->head.store(h, std::memory_order_release);
        return len;
    }
}

void ShmManager::ring_init(RingQueueHeader *q, uint32_t queue_size) {
    // 初始化信号量，pshared=1 表示跨进程共享
    sem_init(&q->sem, 1, 0);
    q->head.store(0, std::memory_order_relaxed);
    q->tail.store(0, std::memory_order_relaxed);
    q->flag.store(Flag::FLAG_ALLOW_WRITE, std::memory_order_relaxed);
    q->data_size.store(queue_size, std::memory_order_relaxed);
    memset(q->data, 0, queue_size);
}

void ShmManager::ring_destroy(RingQueueHeader *q) {
    sem_destroy(&q->sem);
    q->flag.store(0, std::memory_order_relaxed);
}

void ShmManager::ring_reset(RingQueueHeader *q, uint32_t queue_size) {
    ring_destroy(q);
    ring_init(q, queue_size);
}