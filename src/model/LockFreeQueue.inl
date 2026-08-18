/**
 * @file LockFreeQueue.inl
 * @brief 无锁队列模板实现
 * @details 包含队列核心操作的完整实现，采用 CAS 自旋抢占模式，
 * 通过 committed 标志位确保生产者写入完成后消费者才能读取，
 * 使用 m_mask 掩码替代取模运算提升性能。
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
    : m_buffer(new Node[roundUpToPowerOf2(capacity)]),
      m_capacity(roundUpToPowerOf2(capacity)),
      m_mask(roundUpToPowerOf2(capacity) - 1) {
}

template<typename T>
LockFreeQueue<T>::~LockFreeQueue() {
    delete[] m_buffer;
}

template<typename T>
bool LockFreeQueue<T>::push(const T& item) {
    size_t tail = m_tail.load(std::memory_order_acquire);
    size_t head;
    
    while (true) {
        head = m_head.load(std::memory_order_acquire);
        // 通过减法判断是否满：tail - head >= m_capacity 时满
        if (((tail - head) & ~m_mask) != 0) {
            return false;
        }
        
        // CAS抢占队尾位置，成功则跳出循环
        if (m_tail.compare_exchange_weak(tail, tail + 1,
                std::memory_order_acq_rel, std::memory_order_release)) {
            break;
        }
    }
    
    // 计算环形索引，写入数据后标记已提交
    size_t idx = tail & m_mask;
    m_buffer[idx].m_data = item;
    m_buffer[idx].m_committed.store(true, std::memory_order_release);
    return true;
}

template<typename T>
bool LockFreeQueue<T>::push(T&& item) {
    size_t tail = m_tail.load(std::memory_order_acquire);
    size_t head;
    
    while (true) {
        head = m_head.load(std::memory_order_acquire);
        if (((tail - head) & ~m_mask) != 0) {
            return false;
        }
        
        if (m_tail.compare_exchange_weak(tail, tail + 1,
                std::memory_order_acq_rel, std::memory_order_acquire)) {
            break;
        }
    }
    
    size_t idx = tail & m_mask;
    m_buffer[idx].m_data = std::move(item);
    m_buffer[idx].m_committed.store(true, std::memory_order_release);
    return true;
}

template<typename T>
bool LockFreeQueue<T>::pop(T& item) {
    size_t head = m_head.load(std::memory_order_acquire);
    size_t tail, idx;
    
    while (true) {
        tail = m_tail.load(std::memory_order_acquire);
        // 队列为空
        if (head == tail) {
            return false;
        }
        
        // 检查数据是否已提交，未提交则自旋等待
        idx = head & m_mask;
        if (!m_buffer[idx].m_committed.load(std::memory_order_acquire)) {
            continue;
        }
        if (m_head.compare_exchange_weak(head, head + 1,
            std::memory_order_acq_rel, std::memory_order_acquire)) {
            break;
        }
    }
    item = std::move(m_buffer[idx].m_data);
    m_buffer[idx].m_committed.store(false, std::memory_order_release);
    return true;
}

template<typename T>
bool LockFreeQueue<T>::isFull() const {
    size_t head = m_head.load(std::memory_order_acquire);
    size_t tail = m_tail.load(std::memory_order_acquire);
    return ((tail - head) & ~m_mask) != 0;
}

template<typename T>
bool LockFreeQueue<T>::isEmpty() const {
    size_t head = m_head.load(std::memory_order_acquire);
    size_t tail = m_tail.load(std::memory_order_acquire);
    return head == tail;
}

template<typename T>
size_t LockFreeQueue<T>::size() const {
    size_t head = m_head.load(std::memory_order_acquire);
    size_t tail = m_tail.load(std::memory_order_acquire);
    return (tail - head) & m_mask;
}

template<typename T>
void LockFreeQueue<T>::clear() {
    m_head.store(0, std::memory_order_release);
    m_tail.store(0, std::memory_order_release);
    for (size_t i = 0; i < m_capacity; ++i) {
        m_buffer[i].m_committed.store(false, std::memory_order_release);
    }
}