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
    : shm_name_(name), 
    slot_size_(slot_size), 
    slot_count_(slot_count), 
    total_size_(0), 
    shm_fd_(-1),
    shm_ptr_(NULL),
    is_owner_(false) {
    logic_process_id_ = getLogicProcessId(name);
    switch (slot_size_) {
        case SIZE_64B:
            slot_timeout_ = TIMEOUT_64B;
            break;
        case SIZE_1KB:
            slot_timeout_ = TIMEOUT_1KB;
            break;
        case SIZE_256KB:
            slot_timeout_ = TIMEOUT_256KB;
            break;
        default:
            slot_timeout_ = TIMEOUT_64B;
            break;
    }
}

StreamShmCreator::~StreamShmCreator() {
    close();
}

bool StreamShmCreator::create_shm(bool create) {
#if defined(__linux__)
    if (!create) {
        struct stat st;
        if (fstat(shm_fd_, &st) != 0 || st.st_size == 0) {
            LOG_ERROR("StreamShmCreator: fstat failed, st.st_size = %ld", st.st_size);
            return false;
        }
        total_size_ = static_cast<uint32_t>(st.st_size);
    } else {
        total_size_ = sizeof(SMALLRingQueueHeader) + slot_count_ * (offsetof(SMALLDataSlot, data) + slot_size_);
        if (ftruncate(shm_fd_, total_size_) != 0) {
            LOG_ERROR("StreamShmCreator: ftruncate failed, total_size_ = %u", total_size_);
            return false;
        }
    }

    void* ptr = mmap(NULL, total_size_, PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd_, 0);
    if (ptr == MAP_FAILED) {
        shm_ptr_ = NULL;
        LOG_ERROR("StreamShmCreator: mmap failed, total_size_ = %d", total_size_);
        return false;
    }
    shm_ptr_ = ptr;
    SMALLRingQueueHeader* header = static_cast<SMALLRingQueueHeader*>(ptr);

    if (create) {
        sem_init(&header->sem, 1, 0);
        header->slot_size.store(slot_size_, std::memory_order_relaxed);
        header->slot_count.store(slot_count_, std::memory_order_relaxed);
        header->receiver_pid.store(0, std::memory_order_relaxed);
        header->flag.store(Define::BIT0 | Define::BIT1, std::memory_order_relaxed);
    } else {
        if (header->flag.load(std::memory_order_acquire) == 0) {
            return false;
        }
        slot_size_ = header->slot_size.load(std::memory_order_acquire);
        slot_count_ = header->slot_count.load(std::memory_order_acquire);
    }
    return true;
#else
    (void)create;
    return false;
#endif
}

bool StreamShmCreator::open(bool create) {
#if defined(__linux__)
    is_owner_ = false;
    if (create) {
        shm_fd_ = shm_open(shm_name_.c_str(), O_CREAT | O_RDWR | O_EXCL, 0666);
        if (shm_fd_ >= 0) {
            is_owner_ = true;
            if (create_shm(true)) {
                return true;
            }
            close();
            shm_unlink(shm_name_.c_str());
            is_owner_ = false;
            return false;
        }
        shm_fd_ = shm_open(shm_name_.c_str(), O_RDWR, 0666);
        if (shm_fd_ >= 0) {
            return create_shm(false);
        }
        LOG_ERROR("StreamShmCreator: open failed, shm_fd_ = %d", shm_fd_);
    } else {
        shm_fd_ = shm_open(shm_name_.c_str(), O_RDWR, 0666);
        if (shm_fd_ >= 0) {
            return create_shm(false);
        }
        LOG_ERROR("StreamShmCreator: open failed, shm_fd_ = %d", shm_fd_);
    }
    return false;
#else
    (void)create;
    return false;
#endif
}

void StreamShmCreator::delete_shm() {
#if defined(__linux__)
    if (is_owner_) {
        SMALLRingQueueHeader* header = static_cast<SMALLRingQueueHeader*>(shm_ptr_);
        header->flag.store(0, std::memory_order_release);
        sem_post(&header->sem);
        close();
        shm_unlink(shm_name_.c_str());
    }
#endif
}

void StreamShmCreator::close() {
#if defined(__linux__)
    if (shm_ptr_ && shm_ptr_ != MAP_FAILED) {
        munmap(shm_ptr_, total_size_);
        shm_ptr_ = NULL;
    }
    if (shm_fd_ >= 0) {
        ::close(shm_fd_);
        shm_fd_ = -1;
    }
#else
    shm_ptr_ = NULL;
    shm_fd_ = -1;
#endif
}

bool StreamShmCreator::valid() const {
#if defined(__linux__)
    return shm_ptr_ && shm_ptr_ != MAP_FAILED;
#else
    return shm_ptr_ != NULL;
#endif
}

int StreamShmCreator::send(std::shared_ptr<TagSendMessage> buf_msg) {
    if (!valid() || !buf_msg || buf_msg->data.empty()) {
        return -1;
    }
    switch (slot_size_) {
        case SIZE_64B:
            return send_impl(static_cast<SMALLRingQueueHeader*>(shm_ptr_), buf_msg);
        case SIZE_1KB:
            return send_impl(static_cast<MEDIUMRingQueueHeader*>(shm_ptr_), buf_msg);
        case SIZE_256KB:
            return send_impl(static_cast<LARGERingQueueHeader*>(shm_ptr_), buf_msg);
        default:
            return -1;
    }
}

uint32_t StreamShmCreator::recv(std::shared_ptr<TagReceiveMessage> buf_msg) {
    if (!valid() || !buf_msg) {
        return 0;
    }
    switch (slot_size_) {
        case SIZE_64B:
            return recv_impl(static_cast<SMALLRingQueueHeader*>(shm_ptr_), buf_msg);
        case SIZE_1KB:
            return recv_impl(static_cast<MEDIUMRingQueueHeader*>(shm_ptr_), buf_msg);
        case SIZE_256KB:
            return recv_impl(static_cast<LARGERingQueueHeader*>(shm_ptr_), buf_msg);
        default:
            return 0;
    }
}

bool StreamShmCreator::is_empty() {
    if (!valid()) {
        return true;
    }
    auto* h = static_cast<SMALLRingQueueHeader*>(shm_ptr_);
    return h->head.load(std::memory_order_acquire) == h->tail.load(std::memory_order_acquire);
}

bool StreamShmCreator::is_full() {
    if (!valid()) {
        return true;
    }
    auto* h = static_cast<SMALLRingQueueHeader*>(shm_ptr_);
    return (h->tail.load(std::memory_order_acquire) + 1) % slot_count_ == h->head.load(std::memory_order_acquire);
}

std::string StreamShmCreator::get_shm_name() {
    return shm_name_;
}

void StreamShmCreator::set_flag(uint32_t flag) {
    if (!shm_ptr_) {
        return;
    }
    static_cast<SMALLRingQueueHeader*>(shm_ptr_)->flag.store(flag, std::memory_order_release);
}

void StreamShmCreator::wakeup_recv() {
#if defined(__linux__)
    if (!valid()) {
        return;
    }
    auto* hdr = static_cast<SMALLRingQueueHeader*>(shm_ptr_);
    hdr->flag.fetch_and(~static_cast<uint32_t>(Define::BIT1), std::memory_order_release);
    sem_post(&hdr->sem);
#endif
}

uint32_t StreamShmCreator::get_receiver_pid() const {
    if (!valid()) {
        return 0;
    }
    return static_cast<const SMALLRingQueueHeader*>(shm_ptr_)->receiver_pid.load(std::memory_order_acquire);
}

uint32_t StreamShmCreator::get_senders_pid() const {
    if (!valid()) {
        return 0;
    }
    return static_cast<const SMALLRingQueueHeader*>(shm_ptr_)->senders_pid.load(std::memory_order_acquire);
}

void StreamShmCreator::set_receiver_pid(uint32_t receiver_pid) {
    if (!valid()) {
        return;
    }
    static_cast<SMALLRingQueueHeader*>(shm_ptr_)->receiver_pid.store(receiver_pid, std::memory_order_release);
}

void StreamShmCreator::set_senders_pid(uint32_t senders_pid) {
    if (!valid()) {
        return;
    }
    static_cast<SMALLRingQueueHeader*>(shm_ptr_)->senders_pid.store(senders_pid, std::memory_order_release);
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
