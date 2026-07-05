/**
 * @file LockFreeQueue.h
 * @brief 无锁队列模板类
 * @details 基于 CAS 原子操作实现的多生产者多消费者无锁队列，支持泛型数据类型，
 * 使用环形缓冲区 + 幂次容量优化，通过 committed 标志位保证数据可见性，
 * 避免 ABA 问题，适用于高并发场景下的任务投递。
 */

#ifndef LOCKFREE_QUEUE_H
#define LOCKFREE_QUEUE_H

#include <atomic>
#include <cstdint>
#include <utility>

template<typename T>
class LockFreeQueue {
public:
    /**
     * @brief 构造函数
     * @param capacity 队列容量，内部会向上取整为2的幂次
     */
    explicit LockFreeQueue(size_t capacity);
    
    /**
     * @brief 析构函数，释放缓冲区内存
     */
    ~LockFreeQueue();

    LockFreeQueue(const LockFreeQueue&) = delete;
    LockFreeQueue& operator=(const LockFreeQueue&) = delete;

    /**
     * @brief 入队（拷贝语义）
     * @param item 待入队的元素引用
     * @return 成功返回true，队列满返回false
     */
    bool push(const T& item);
    
    /**
     * @brief 入队（移动语义）
     * @param item 待入队的元素右值引用
     * @return 成功返回true，队列满返回false
     */
    bool push(T&& item);
    
    /**
     * @brief 出队
     * @param item 输出参数，存储出队元素
     * @return 成功返回true，队列空返回false
     */
    bool pop(T& item);
    
    /**
     * @brief 判断队列是否已满
     * @return 满返回true，否则返回false
     */
    bool isFull() const;
    
    /**
     * @brief 判断队列是否为空
     * @return 空返回true，否则返回false
     */
    bool isEmpty() const;
    
    /**
     * @brief 获取队列当前元素数量
     * @return 当前元素个数
     */
    size_t size() const;

private:
    /**
     * @brief 节点结构，包含数据和提交标志位
     */
    struct Node {
        T data;
        std::atomic<bool> committed{false}; // 标记数据是否已写入完成
    };

    /**
     * @brief 将容量向上取整为2的幂次
     * @param n 原始容量
     * @return 不小于n的最小2的幂次
     */
    static size_t roundUpToPowerOf2(size_t n);

    Node* buffer_;           // 环形缓冲区
    const size_t capacity_;  // 队列容量（2的幂次）
    const size_t mask_;      // 索引掩码，用于快速取模
    std::atomic<size_t> head_{0};   // 队头指针（出队位置）
    std::atomic<size_t> tail_{0};   // 队尾指针（入队位置）
};

#include "LockFreeQueue.inl"

#endif