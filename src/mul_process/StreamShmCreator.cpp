#include "StreamShmCreator.h"
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <chrono>
#if defined(__linux__)
#include <unistd.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <sys/stat.h>
#endif

namespace IpcInterface {
namespace MulProcess {

StreamShmCreator::StreamShmCreator(const std::string& name, uint32_t slot_size, uint32_t slot_count) 
    : m_shm_name(name), 
    m_slot_size(slot_size), 
    m_slot_count(slot_count), 
    m_total_size(0), 
    m_shm_fd(-1),
    m_shm_ptr(NULL),
    m_is_owner(false) {
    m_logic_process_id = getLogicProcessId(name);
    switch (m_slot_size) {
        case SIZE_64B:
            m_slot_timeout = TIMEOUT_64B;
            break;
        case SIZE_1KB:
            m_slot_timeout = TIMEOUT_1KB;
            break;
        case SIZE_256KB:
            m_slot_timeout = TIMEOUT_256KB;
            break;
        default:
            m_slot_timeout = TIMEOUT_64B;
            break;
    }
}

StreamShmCreator::~StreamShmCreator() {
    Close();
}

bool StreamShmCreator::create_shm(bool create) {
#if defined(__linux__)
    if (!create) {
        struct stat st;
        if (fstat(m_shm_fd, &st) != 0 || st.st_size == 0) {
            LOG_ERROR("StreamShmCreator: fstat failed, st.st_size = %ld", st.st_size);
            return false;
        }
        m_total_size = static_cast<uint32_t>(st.st_size);
    } else {
        m_total_size = sizeof(SMALLRingQueueHeader) + m_slot_count * (offsetof(SMALLDataSlot, m_data) + m_slot_size);
        if (ftruncate(m_shm_fd, m_total_size) != 0) {
            LOG_ERROR("StreamShmCreator: ftruncate failed, m_total_size = %u", m_total_size);
            return false;
        }
    }

    void* ptr = mmap(NULL, m_total_size, PROT_READ | PROT_WRITE, MAP_SHARED, m_shm_fd, 0);
    if (ptr == MAP_FAILED) {
        m_shm_ptr = NULL;
        LOG_ERROR("StreamShmCreator: mmap failed, m_total_size = %d", m_total_size);
        return false;
    }
    m_shm_ptr = ptr;
    SMALLRingQueueHeader* header = static_cast<SMALLRingQueueHeader*>(ptr);

    if (create) {
        sem_init(&header->m_sem, 1, 0);
        header->m_slot_size.store(m_slot_size, std::memory_order_relaxed);
        header->m_slot_count.store(m_slot_count, std::memory_order_relaxed);
        header->m_receiver_pid.store(0, std::memory_order_relaxed);
        header->m_flag.store(Define::BIT0 | Define::BIT1, std::memory_order_relaxed);
    } else {
        if (header->m_flag.load(std::memory_order_acquire) == 0) {
            return false;
        }
        m_slot_size = header->m_slot_size.load(std::memory_order_acquire);
        m_slot_count = header->m_slot_count.load(std::memory_order_acquire);
    }
    return true;
#else
    (void)create;
    return false;
#endif
}

bool StreamShmCreator::Open(bool create) {
#if defined(__linux__)
    m_is_owner = false;
    if (create) {
        m_shm_fd = shm_open(m_shm_name.c_str(), O_CREAT | O_RDWR | O_EXCL, 0666);
        if (m_shm_fd >= 0) {
            m_is_owner = true;
            if (create_shm(true)) {
                return true;
            }
            Close();
            shm_unlink(m_shm_name.c_str());
            m_is_owner = false;
            return false;
        }
        m_shm_fd = shm_open(m_shm_name.c_str(), O_RDWR, 0666);
        if (m_shm_fd >= 0) {
            return create_shm(false);
        }
        LOG_ERROR("StreamShmCreator: open failed, m_shm_fd = %d", m_shm_fd);
    } else {
        m_shm_fd = shm_open(m_shm_name.c_str(), O_RDWR, 0666);
        if (m_shm_fd >= 0) {
            return create_shm(false);
        }
        LOG_ERROR("StreamShmCreator: open failed, m_shm_fd = %d", m_shm_fd);
    }
    return false;
#else
    (void)create;
    return false;
#endif
}

void StreamShmCreator::delete_shm() {
#if defined(__linux__)
    if (m_is_owner) {
        SMALLRingQueueHeader* header = static_cast<SMALLRingQueueHeader*>(m_shm_ptr);
        header->m_flag.store(0, std::memory_order_release);
        sem_post(&header->m_sem);
        Close();
        shm_unlink(m_shm_name.c_str());
    }
#endif
}

void StreamShmCreator::Close() {
#if defined(__linux__)
    if (m_shm_ptr && m_shm_ptr != MAP_FAILED) {
        munmap(m_shm_ptr, m_total_size);
        m_shm_ptr = NULL;
    }
    if (m_shm_fd >= 0) {
        ::close(m_shm_fd);
        m_shm_fd = -1;
    }
#else
    m_shm_ptr = NULL;
    m_shm_fd = -1;
#endif
}

bool StreamShmCreator::valid() const {
#if defined(__linux__)
    return m_shm_ptr && m_shm_ptr != MAP_FAILED;
#else
    return m_shm_ptr != NULL;
#endif
}

int StreamShmCreator::send(std::shared_ptr<TagSendMessage> buf_msg) {
    if (!valid() || !buf_msg || buf_msg->m_data.empty()) {
        return -1;
    }
    switch (m_slot_size) {
        case SIZE_64B:
            return send_impl(static_cast<SMALLRingQueueHeader*>(m_shm_ptr), buf_msg);
        case SIZE_1KB:
            return send_impl(static_cast<MEDIUMRingQueueHeader*>(m_shm_ptr), buf_msg);
        case SIZE_256KB:
            return send_impl(static_cast<LARGERingQueueHeader*>(m_shm_ptr), buf_msg);
        default:
            return -1;
    }
}

uint32_t StreamShmCreator::recv(std::shared_ptr<TagReceiveMessage> buf_msg) {
    if (!valid() || !buf_msg) {
        return 0;
    }
    switch (m_slot_size) {
        case SIZE_64B:
            return recv_impl(static_cast<SMALLRingQueueHeader*>(m_shm_ptr), buf_msg);
        case SIZE_1KB:
            return recv_impl(static_cast<MEDIUMRingQueueHeader*>(m_shm_ptr), buf_msg);
        case SIZE_256KB:
            return recv_impl(static_cast<LARGERingQueueHeader*>(m_shm_ptr), buf_msg);
        default:
            return 0;
    }
}

bool StreamShmCreator::is_empty() {
    if (!valid()) {
        return true;
    }
    auto* h = static_cast<SMALLRingQueueHeader*>(m_shm_ptr);
    return h->m_head.load(std::memory_order_acquire) == h->m_tail.load(std::memory_order_acquire);
}

bool StreamShmCreator::is_full() {
    if (!valid()) {
        return true;
    }
    auto* h = static_cast<SMALLRingQueueHeader*>(m_shm_ptr);
    return (h->m_tail.load(std::memory_order_acquire) + 1) % m_slot_count == h->m_head.load(std::memory_order_acquire);
}

std::string StreamShmCreator::get_shm_name() {
    return m_shm_name;
}

void StreamShmCreator::set_flag(uint32_t flag) {
    if (!m_shm_ptr) {
        return;
    }
    static_cast<SMALLRingQueueHeader*>(m_shm_ptr)->m_flag.store(flag, std::memory_order_release);
}

void StreamShmCreator::wakeup_recv() {
#if defined(__linux__)
    if (!valid()) {
        return;
    }
    auto* hdr = static_cast<SMALLRingQueueHeader*>(m_shm_ptr);
    hdr->m_flag.fetch_and(~static_cast<uint32_t>(Define::BIT1), std::memory_order_release);
    sem_post(&hdr->m_sem);
#endif
}

uint32_t StreamShmCreator::get_receiver_pid() const {
    if (!valid()) {
        return 0;
    }
    return static_cast<const SMALLRingQueueHeader*>(m_shm_ptr)->m_receiver_pid.load(std::memory_order_acquire);
}

uint32_t StreamShmCreator::get_senders_pid() const {
    if (!valid()) {
        return 0;
    }
    return static_cast<const SMALLRingQueueHeader*>(m_shm_ptr)->m_senders_pid.load(std::memory_order_acquire);
}

void StreamShmCreator::set_receiver_pid(uint32_t receiver_pid) {
    if (!valid()) {
        return;
    }
    static_cast<SMALLRingQueueHeader*>(m_shm_ptr)->m_receiver_pid.store(receiver_pid, std::memory_order_release);
}

void StreamShmCreator::set_senders_pid(uint32_t senders_pid) {
    if (!valid()) {
        return;
    }
    static_cast<SMALLRingQueueHeader*>(m_shm_ptr)->m_senders_pid.store(senders_pid, std::memory_order_release);
}

uint8_t StreamShmCreator::getLogicProcessId(const std::string& shm_name) {
    for (uint32_t i = 0; i < Define::kShmNameCount; i++) {
        if (shm_name == Define::kShmNames[i]) {
            return i;
        }
    }
    return Define::INVALID_FD;
}

uint64_t StreamShmCreator::get_timestamp() {
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count());
}

} // namespace MulProcess
} // namespace IpcInterface
