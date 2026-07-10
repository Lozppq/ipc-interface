#include "ShmCreator.h"
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <unistd.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <chrono>

namespace IpcInterface {
namespace MulProcess {

ShmCreator::ShmCreator(const std::string& name, SlotSize size, uint32_t slot_count) : shm_name_(name), slot_size_(size), slot_count_(slot_count) {
    
}

ShmCreator::~ShmCreator() {
    close();
}

void ShmCreator::create(bool create) {
    switch (slot_size_) {
        case SlotSize::SIZE_64B:
            SMALLRingQueueHeader* header = (SMALLRingQueueHeader*)mmap(nullptr, sizeof(SMALLRingQueueHeader) + slot_count_ * sizeof(SMALLDataSlot), PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd_, 0);
            if (header != MAP_FAILED) {
                shm_ptr_ = header;
                if (create) {
                    sem_init(&header->sem, 1, 0);
                    header->slot_size = slot_count_;
                }
            }
            break;
        case SlotSize::SIZE_1KB:
            MEDIUMRingQueueHeader* header = (MEDIUMRingQueueHeader*)mmap(nullptr, sizeof(MEDIUMRingQueueHeader) + slot_count_ * sizeof(MEDIUMDataSlot), PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd_, 0);
            if (header != MAP_FAILED) {
                shm_ptr_ = header;
                if (create) {
                    sem_init(&header->sem, 1, 0);
                    header->slot_size = slot_count_;
                }
            }
            break;
        case SlotSize::SIZE_256KB:
            LARGERingQueueHeader* header = (LARGERingQueueHeader*)mmap(nullptr, sizeof(LARGERingQueueHeader) + slot_count_ * sizeof(LARGEDataSlot), PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd_, 0);
            if (header != MAP_FAILED) {
                shm_ptr_ = header;
                if (create) {
                    sem_init(&header->sem, 1, 0);
                    header->slot_size = slot_count_;
                }
            }
            break;
        default:
            printf("ShmCreator: create failed, slot_count_ = %d\n", slot_count_);
            return;
    }
}

bool ShmCreator::open() {
    shm_fd_ = shm_open(shm_name_.c_str(), O_CREAT | O_RDWR | O_EXCL, 0666);
    if (shm_fd_ >= 0) {
        create(true);
    } else {
        shm_fd_ = shm_open(shm_name_.c_str(), O_RDWR, 0666);
        if (shm_fd_ >= 0) {
            create(false);
        } else {
            printf("ShmCreator: open failed, shm_fd_ = %d\n", shm_fd_);
        }
    }

    return false;
}

void ShmCreator::close() {
}

bool ShmCreator::valid() const {
    return false;
}

int ShmCreator::send(const std::vector<uint8_t>& msg) {
    (void)msg;
    return -1;
}

uint32_t ShmCreator::recv(std::vector<uint8_t>& buf) {
    (void)buf;
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
