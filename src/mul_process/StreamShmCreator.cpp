#include "StreamShmCreator.h"
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <unistd.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <chrono>

namespace IpcInterface {
namespace MulProcess {

StreamShmCreator::StreamShmCreator(const std::string& name, uint32_t slot_size, uint32_t slot_count) 
    : shm_name_(name), 
    slot_size_(slot_size), 
    slot_count_(slot_count), 
    total_size_(0), 
    client_info_(NULL),
    shm_fd_(-1),
    shm_ptr_(NULL),
    is_owner_(false) {
    
}

StreamShmCreator::~StreamShmCreator() {
    close();
}

bool StreamShmCreator::create_shm(bool create) {
    if (!create) {
        struct stat st;
        if (fstat(shm_fd_, &st) != 0 || st.st_size == 0) {
            printf("StreamShmCreator: fstat failed, st.st_size = %ld\n", st.st_size);
            return false;
        }
        total_size_ = static_cast<uint32_t>(st.st_size);
    } else {
        total_size_ = sizeof(SMALLRingQueueHeader) + slot_count_ * (offsetof(SMALLDataSlot, data) + slot_size_);
        ftruncate(shm_fd_, total_size_);
    }

    void* ptr = mmap(NULL, total_size_, PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd_, 0);
    if (ptr == MAP_FAILED) {
        shm_ptr_ = NULL;
        printf("StreamShmCreator: mmap failed, total_size_ = %d\n", total_size_);
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
}

bool StreamShmCreator::open(bool create) {
    is_owner_ = create;
    if (create) {
        shm_fd_ = shm_open(shm_name_.c_str(), O_CREAT | O_RDWR | O_EXCL, 0666);
        if (shm_fd_ >= 0) {
            return create_shm(true);
        } else {
            shm_fd_ = shm_open(shm_name_.c_str(), O_RDWR, 0666);
            if (shm_fd_ >= 0) {
                return create_shm(false);
            } else {
                printf("StreamShmCreator: open failed, shm_fd_ = %d\n", shm_fd_);
            }
        }
    } else {
        shm_fd_ = shm_open(shm_name_.c_str(), O_RDWR, 0666);
        if (shm_fd_ >= 0) {
            return create_shm(false);
        } else {
            printf("StreamShmCreator: open failed, shm_fd_ = %d\n", shm_fd_);
        }
    }
    return false;
}

void StreamShmCreator::delete_shm() {
    if (is_owner_) {
        close();
        shm_unlink(shm_name_.c_str());
    }
}

void StreamShmCreator::close() {
    if (shm_ptr_ && shm_ptr_ != MAP_FAILED) {
        munmap(shm_ptr_, total_size_);
        shm_ptr_ = NULL;
    }
    if (shm_fd_ >= 0) {
        ::close(shm_fd_);
        shm_fd_ = -1;
    }
}

bool StreamShmCreator::valid() const {
    return shm_ptr_ && shm_ptr_ != MAP_FAILED;
}

int StreamShmCreator::send(const std::vector<uint8_t>& msg) {
    if (!valid() || msg.empty()) {
        return -1;
    }
    switch (slot_size_) {
        case SIZE_64B:
            return send_impl(static_cast<SMALLRingQueueHeader*>(shm_ptr_), msg);
        case SIZE_1KB:
            return send_impl(static_cast<MEDIUMRingQueueHeader*>(shm_ptr_), msg);
        case SIZE_256KB:
            return send_impl(static_cast<LARGERingQueueHeader*>(shm_ptr_), msg);
        default:
            return -1;
    }
}

uint32_t StreamShmCreator::recv(std::vector<uint8_t>& buf) {
    if (!valid()) {
        return 0;
    }
    switch (slot_size_) {
        case SIZE_64B:
            return recv_impl(static_cast<SMALLRingQueueHeader*>(shm_ptr_), buf);
        case SIZE_1KB:
            return recv_impl(static_cast<MEDIUMRingQueueHeader*>(shm_ptr_), buf);
        case SIZE_256KB:
            return recv_impl(static_cast<LARGERingQueueHeader*>(shm_ptr_), buf);
        default:
            return 0;
    }
}

bool StreamShmCreator::is_empty() {
    if (!valid()) {
        return true;
    }
    switch (slot_size_) {
        case SIZE_64B:
            return is_empty_impl(static_cast<SMALLRingQueueHeader*>(shm_ptr_));
        case SIZE_1KB:
            return is_empty_impl(static_cast<MEDIUMRingQueueHeader*>(shm_ptr_));
        case SIZE_256KB:
            return is_empty_impl(static_cast<LARGERingQueueHeader*>(shm_ptr_));
        default:
            return true;
    }
}

bool StreamShmCreator::is_full() {
    if (!valid()) {
        return true;
    }
    switch (slot_size_) {
        case SIZE_64B:
            return is_full_impl(static_cast<SMALLRingQueueHeader*>(shm_ptr_));
        case SIZE_1KB:
            return is_full_impl(static_cast<MEDIUMRingQueueHeader*>(shm_ptr_));
        case SIZE_256KB:
            return is_full_impl(static_cast<LARGERingQueueHeader*>(shm_ptr_));
        default:
            return true;
    }
}

std::string StreamShmCreator::get_shm_name() {
    return shm_name_;
}

void StreamShmCreator::set_client_info(ClientStatusInfo* client_info) {
    client_info_ = client_info;
}

void StreamShmCreator::set_flag(uint32_t flag) {
    if (!shm_ptr_) {
        return;
    }
    switch (slot_size_) {
        case SIZE_64B:
            set_flag_impl(static_cast<SMALLRingQueueHeader*>(shm_ptr_), flag);
            break;
        case SIZE_1KB:
            set_flag_impl(static_cast<MEDIUMRingQueueHeader*>(shm_ptr_), flag);
            break;
        case SIZE_256KB:
            set_flag_impl(static_cast<LARGERingQueueHeader*>(shm_ptr_), flag);
            break;
        default:
            break;
    }
}

} // namespace MulProcess
} // namespace IpcInterface
