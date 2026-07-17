#include "ShmCreator.h"
#include <cstdint>
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

ShmCreator::ShmCreator(const std::string& name, uint32_t size, uint32_t slot_count) : shm_name_(name), slot_size_(size), slot_count_(slot_count), total_size_(0) {
    
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
        case SIZE_64B:
            if (create) {
                total_size_ = sizeof(SMALLRingQueueHeader) + slot_count_ * sizeof(SMALLDataSlot);
                ftruncate(shm_fd_, total_size_);
            }
            SMALLRingQueueHeader* header = (SMALLRingQueueHeader*)mmap(nullptr, total_size_, PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd_, 0);
            if (header != MAP_FAILED) {
                shm_ptr_ = header;
                if (create) {
                    sem_init(&header->sem, 1, 0);
                    header->slot_size.store(slot_size_, std::memory_order_relaxed);
                    header->slot_count.store(slot_count_, std::memory_order_relaxed);
                    header->flag.store(Define::BitEnum::BIT0 | Define::BitEnum::BIT1, std::memory_order_relaxed);
                } else {
                    if (header->flag.load(std::memory_order_acquire) == 0) {
                        return false;
                    }
                    slot_size_ = header->slot_size.load(std::memory_order_acquire);
                    slot_count_ = header->slot_count.load(std::memory_order_acquire);
                }
                return true;
            }
            break;
        case SIZE_1KB:
            if (create) {
                total_size_ = sizeof(MEDIUMRingQueueHeader) + slot_count_ * sizeof(MEDIUMDataSlot);
                ftruncate(shm_fd_, total_size_);
            }
            MEDIUMRingQueueHeader* header = (MEDIUMRingQueueHeader*)mmap(nullptr, total_size_, PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd_, 0);
            if (header != MAP_FAILED) {
                shm_ptr_ = header;
                if (create) {
                    sem_init(&header->sem, 1, 0);
                    header->slot_size.store(slot_size_, std::memory_order_relaxed);
                    header->slot_count.store(slot_count_, std::memory_order_relaxed);
                    header->flag.store(Define::BitEnum::BIT0 | Define::BitEnum::BIT1, std::memory_order_relaxed);
                } else {
                    if (header->flag.load(std::memory_order_acquire) == 0) {
                        return false;
                    }
                    slot_size_ = header->slot_size.load(std::memory_order_acquire);
                    slot_count_ = header->slot_count.load(std::memory_order_acquire);
                }
                return true;
            }
            break;
        case SIZE_256KB:
            if (create) {
                total_size_ = sizeof(LARGERingQueueHeader) + slot_count_ * sizeof(LARGEDataSlot);
                ftruncate(shm_fd_, total_size_);
            }
            LARGERingQueueHeader* header = (LARGERingQueueHeader*)mmap(nullptr, total_size_, PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd_, 0);
            if (header != MAP_FAILED) {
                shm_ptr_ = header;
                if (create) {
                    sem_init(&header->sem, 1, 0);
                    header->slot_size.store(slot_size_, std::memory_order_relaxed);
                    header->slot_count.store(slot_count_, std::memory_order_relaxed);
                    header->flag.store(Define::BitEnum::BIT0 | Define::BitEnum::BIT1, std::memory_order_relaxed);
                } else {
                    if (header->flag.load(std::memory_order_acquire) == 0) {
                        return false;
                    }
                    slot_size_ = header->slot_size.load(std::memory_order_acquire);
                    slot_count_ = header->slot_count.load(std::memory_order_acquire);
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

    // 这里把数据写入到共享内存中
    uint32_t old_tail, new_tail;
    const uint32_t total_len = msg.size() + 4;
    if (slot_size_ == 0 || slot_count_ == 0) {
        return -1;
    }
    // 向上取整，计算需要多少个数据区
    const uint32_t slot_need = (total_len + slot_size_ - 1) / slot_size_; 

    // CAS抢占队尾位置
    while (1) {
        old_tail = shm_ptr_->tail.load(std::memory_order_acquire);
        new_tail = (old_tail + slot_need) % slot_count_;

        // 检查空间是否足够
        uint32_t h = shm_ptr_->head.load(std::memory_order_acquire);
        uint32_t available = (h - old_tail - 1 + slot_count_) % slot_count_;
        if (available >= slot_need) {
            if (shm_ptr_->tail.compare_exchange_weak(old_tail, new_tail, std::memory_order_acquire, std::memory_order_relaxed)) {
                break;
            }
        } else {
            return -1;
        }
    }

    // 把数据写入到共享内存中，一个slot的写进去
    uint32_t t_msg_index = 0;
    uint32_t t_slice_id = 0;
    while (t_msg_index < msg.size()) {
        uint32_t t_len = slot_size_;
        
        // 第一个slice记录总长度和slice信息
        if (t_msg_index == 0){
            t_len -= 4;
            memcpy(shm_ptr_->data[old_tail].data, total_len - 4, 4);
            memcpy(shm_ptr_->data[old_tail].data + 4, msg.data(), (slot_need > 1) ? t_len : msg.size());
            shm_ptr_->data[old_tail].slice_id.store(t_slice_id, std::memory_order_release);
            shm_ptr_->data[old_tail].slice_count.store(slot_need, std::memory_order_release);
        } else {
            memcpy(shm_ptr_->data[old_tail].data, msg.data() + t_msg_index, (msg.size() - t_msg_index >= t_len) ? t_len : msg.size() - t_msg_index);
        }

        // 提交slice信息
        shm_ptr_->data[old_tail].commit.store(true, std::memory_order_release);
        old_tail = (old_tail + 1) % slot_count_;
        t_slice_id += 1;
        t_msg_index += t_len;
    }

    // 提交信号量
    sem_post(&shm_ptr_->sem);

    return msg.size();
}

uint32_t ShmCreator::recv(std::vector<uint8_t>& buf) {
    if (!valid() || (shm_ptr_->flag.load(std::memory_order_acquire) & Define::BitEnum::BIT1) == 0) {
        return 0;
    }

    // 等待信号量
    sem_wait(&shm_ptr_->sem);

    // 不允许接收，退出逻辑需要清理
    if ((shm_ptr_->flag.load(std::memory_order_acquire) & Define::BitEnum::BIT1) == 0) {
        return 0;
    }

    uint32_t head, slice_count = 0, t_msg_index = 0;
    // 获取数据
    while (1) {
        head = shm_ptr_->head.load(std::memory_order_acquire);
        // 如果已经提交了标志位
        if (shm_ptr_->data[head].commit.load(std::memory_order_acquire)) {
            if (slice_count == 0) {
                slice_count = shm_ptr_->data[head].slice_count.load(std::memory_order_acquire);
                shm_ptr_->data[head].slice_id.store(0, std::memory_order_release);
                shm_ptr_->data[head].slice_count.store(0, std::memory_order_release);
                // 预设buf的总大小为第一个slice的data前四个字节组成的无符号数大小
                uint32_t total_len = 0;
                memcpy(&total_len, shm_ptr_->data[head].data, 4);
                if (total_len == 0) {
                    shm_ptr_->head.store(head + 1, std::memory_order_release);
                    shm_ptr_->data[head].commit.store(false, std::memory_order_release);
                    slice_count = 0;
                    continue;
                }
                buf.resize(total_len);
                memcpy(buf.data(), shm_ptr_->data[head].data + 4, slot_size_ - 4);
                t_msg_index += slot_size_ - 4;
            } else {
                memcpy(buf.data() + t_msg_index, shm_ptr_->data[head].data, slot_size_);
                t_msg_index += slot_size_;
            }
            shm_ptr_->data[head].commit.store(false, std::memory_order_release);
            if (slice_count == shm_ptr_->data[head].slice_id.load(std::memory_order_acquire)) {
                break;
            }
            head = (head + 1) % slot_count;
        }
    }
    shm_ptr_->head.store(head, std::memory_order_release);
    return t_msg_index;
}

bool ShmCreator::is_empty() {
    return shm_ptr_->head.load(std::memory_order_acquire) == shm_ptr_->tail.load(std::memory_order_acquire);
}

bool ShmCreator::is_full() {
    return (shm_ptr_->tail.load(std::memory_order_acquire) + 1) % slot_count == shm_ptr_->head.load(std::memory_order_acquire);
}

std::string ShmCreator::get_shm_name() {
    return shm_name_;
}

} // namespace MulProcess
} // namespace IpcInterface
