/**
 * @file LockFreeQueue.inl
 * @brief 无锁队列模板实现
 * @details 包含队列核心操作的完整实现，采用 CAS 自旋抢占模式，
 * 通过 committed 标志位确保生产者写入完成后消费者才能读取，
 * 使用 mask_ 掩码替代取模运算提升性能。
 */

template<typename T>
size_t LockFreeQueue<T>::roundUpToPowerOf2(size_t n) {
    size_t res = 1;
    while (res < n) {
        res <<= 1;
    }
    return res;
}

template<typename T>
LockFreeQueue<T>::LockFreeQueue(size_t capacity)
    : capacity_(roundUpToPowerOf2(capacity)),
      mask_(roundUpToPowerOf2(capacity) - 1),
      buffer_(new Node[roundUpToPowerOf2(capacity)]) {
}

template<typename T>
LockFreeQueue<T>::~LockFreeQueue() {
    delete[] buffer_;
}

template<typename T>
bool LockFreeQueue<T>::push(const T& item) {
    size_t tail = tail_.load(std::memory_order_relaxed);
    size_t head;
    
    while (true) {
        head = head_.load(std::memory_order_acquire);
        // 通过减法判断是否满：tail - head >= capacity_ 时满
        if (((tail - head) & ~mask_) != 0) {
            return false;
        }
        
        // CAS抢占队尾位置，成功则跳出循环
        if (tail_.compare_exchange_weak(tail, tail + 1,
                std::memory_order_acq_rel, std::memory_order_relaxed)) {
            break;
        }
    }
    
    // 计算环形索引，写入数据后标记已提交
    size_t idx = tail & mask_;
    buffer_[idx].data = item;
    buffer_[idx].committed.store(true, std::memory_order_release);
    return true;
}

template<typename T>
bool LockFreeQueue<T>::push(T&& item) {
    size_t tail = tail_.load(std::memory_order_relaxed);
    size_t head;
    
    while (true) {
        head = head_.load(std::memory_order_acquire);
        if (((tail - head) & ~mask_) != 0) {
            return false;
        }
        
        if (tail_.compare_exchange_weak(tail, tail + 1,
                std::memory_order_acq_rel, std::memory_order_relaxed)) {
            break;
        }
    }
    
    size_t idx = tail & mask_;
    buffer_[idx].data = std::move(item);
    buffer_[idx].committed.store(true, std::memory_order_release);
    return true;
}

template<typename T>
bool LockFreeQueue<T>::pop(T& item) {
    size_t head = head_.load(std::memory_order_relaxed);
    size_t tail;
    
    while (true) {
        tail = tail_.load(std::memory_order_acquire);
        // 队列为空
        if (head == tail) {
            return false;
        }
        
        // 检查数据是否已提交，未提交则自旋等待
        size_t idx = head & mask_;
        if (!buffer_[idx].committed.load(std::memory_order_acquire)) {
            continue;
        }
        
        // 移动数据，重置提交标志，CAS更新队头
        item = std::move(buffer_[idx].data);
        buffer_[idx].committed.store(false, std::memory_order_relaxed);
        
        if (head_.compare_exchange_weak(head, head + 1,
                std::memory_order_acq_rel, std::memory_order_relaxed)) {
            return true;
        }
    }
}

template<typename T>
bool LockFreeQueue<T>::isFull() const {
    size_t head = head_.load(std::memory_order_acquire);
    size_t tail = tail_.load(std::memory_order_acquire);
    return ((tail - head) & ~mask_) != 0;
}

template<typename T>
bool LockFreeQueue<T>::isEmpty() const {
    size_t head = head_.load(std::memory_order_acquire);
    size_t tail = tail_.load(std::memory_order_acquire);
    return head == tail;
}

template<typename T>
size_t LockFreeQueue<T>::size() const {
    size_t head = head_.load(std::memory_order_acquire);
    size_t tail = tail_.load(std::memory_order_acquire);
    return (tail - head) & mask_;
}
