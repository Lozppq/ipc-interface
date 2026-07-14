#include "ShmCreator.h"
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <unistd.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <chrono>
#include "../define/common.h"

namespace IpcInterface {
namespace MulProcess {

ShmCreator::ShmCreator(const std::string& name, SlotSize size, uint32_t slot_count) : shm_name_(name), slot_size_(size), slot_count_(slot_count), total_size_(0) {
    
}

ShmCreator::~ShmCreator() {
    close();
}

bool ShmCreator::create_shm(bool create) {
    if (!create) {
        struct stat st;
        if (fstat(shm_fd_, &st) != 0 || st.st_size == 0) {
            printf("ShmCreator: fstat failed, st.st_size = %ld\n", st.st_size);
            return false;
        }
        total_size_ = static_cast<uint32_t>(st.st_size);
    }

    switch (slot_size_) {
        case SlotSize::SIZE_64B:
            if (create) {
                total_size_ = sizeof(SMALLRingQueueHeader) + slot_count_ * sizeof(SMALLDataSlot);
                ftruncate(shm_fd_, total_size_);
            }
            SMALLRingQueueHeader* header = (SMALLRingQueueHeader*)mmap(nullptr, total_size_, PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd_, 0);
            if (header != MAP_FAILED) {
                shm_ptr_ = header;
                if (create) {
                    sem_init(&header->sem, 1, 0);
                    header->slot_size.store(slot_count_, std::memory_order_relaxed);
                    header->flag.store(Define::BitEnum::BIT0 | Define::BitEnum::BIT1, std::memory_order_relaxed);
                }
                return true;
            }
            break;
        case SlotSize::SIZE_1KB:
            if (create) {
                total_size_ = sizeof(MEDIUMRingQueueHeader) + slot_count_ * sizeof(MEDIUMDataSlot);
                ftruncate(shm_fd_, total_size_);
            }
            MEDIUMRingQueueHeader* header = (MEDIUMRingQueueHeader*)mmap(nullptr, total_size_, PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd_, 0);
            if (header != MAP_FAILED) {
                shm_ptr_ = header;
                if (create) {
                    sem_init(&header->sem, 1, 0);
                    header->slot_size.store(slot_count_, std::memory_order_relaxed);
                    header->flag.store(Define::BitEnum::BIT0 | Define::BitEnum::BIT1, std::memory_order_relaxed);
                }
                return true;
            }
            break;
        case SlotSize::SIZE_256KB:
            if (create) {
                total_size_ = sizeof(LARGERingQueueHeader) + slot_count_ * sizeof(LARGEDataSlot);
                ftruncate(shm_fd_, total_size_);
            }
            LARGERingQueueHeader* header = (LARGERingQueueHeader*)mmap(nullptr, total_size_, PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd_, 0);
            if (header != MAP_FAILED) {
                shm_ptr_ = header;
                if (create) {
                    sem_init(&header->sem, 1, 0);
                    header->slot_size.store(slot_count_, std::memory_order_relaxed);
                    header->flag.store(Define::BitEnum::BIT0 | Define::BitEnum::BIT1, std::memory_order_relaxed);
                }
                return true;
            }
            break;
        default:
            printf("ShmCreator: create failed, slot_count_ = %d\n", slot_count_);
            break;
    }
    return false;
}

bool ShmCreator::open() {
    shm_fd_ = shm_open(shm_name_.c_str(), O_CREAT | O_RDWR | O_EXCL, 0666);
    if (shm_fd_ >= 0) {
        return create_shm(true);
    } else {
        shm_fd_ = shm_open(shm_name_.c_str(), O_RDWR, 0666);
        if (shm_fd_ >= 0) {
            return create_shm(false);
        } else {
            printf("ShmCreator: open failed, shm_fd_ = %d\n", shm_fd_);
        }
    }

    return false;
}

void ShmCreator::delete_shm() {
    close();
    shm_unlink(shm_name_.c_str());
}

void ShmCreator::close() {
    if (shm_ptr_ && shm_ptr_ != MAP_FAILED) {
        munmap(shm_ptr_, total_size_);
        shm_ptr_ = nullptr;
    }
    if (shm_fd_ >= 0) {
        ::close(shm_fd_);
        shm_fd_ = -1;
    }
}

bool ShmCreator::valid() const {
    return shm_ptr_ && shm_ptr_ != MAP_FAILED;
}

int ShmCreator::send(const std::vector<uint8_t>& msg) {
    if (!valid() || msg.empty() || (shm_ptr_->flag.load(std::memory_order_acquire) & Define::BitEnum::BIT0) == 0) {
        return -1;
    }
    
    return -1;
}

uint32_t ShmCreator::recv(std::vector<uint8_t>& buf) {
    if (!valid() || (shm_ptr_->flag.load(std::memory_order_acquire) & Define::BitEnum::BIT1) == 0) {
        return 0;
    }
    return 0;
}

bool ShmCreator::is_empty() {
    return true;
}

bool ShmCreator::is_full(uint32_t len) {
    (void)len;
    return false;
}

} // namespace MulProcess
} // namespace IpcInterface
