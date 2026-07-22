#include "ShmCreator.h"
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <cstdio>

namespace IpcInterface {
namespace MulProcess {

template<typename T>
ShmCreator<T>::ShmCreator(const std::string& name, uint32_t total_size) 
    : shm_name_(name), 
    total_size_(total_size), 
    shm_fd_(-1),
    shm_ptr_(NULL),
    is_owner_(false) {
    
}

template<typename T>
ShmCreator<T>::~ShmCreator() {
    close();
}

template<typename T>
bool ShmCreator<T>::create_shm(bool create) {
    if (!create) {
        struct stat st;
        if (fstat(shm_fd_, &st) != 0 || st.st_size == 0) {
            printf("ShmCreator: fstat failed, st.st_size = %ld\n", st.st_size);
            return false;
        }
        total_size_ = static_cast<uint32_t>(st.st_size);
    }

    // 映射共享内存，按照T*类型映射
    shm_ptr_ = reinterpret_cast<T*>(mmap(NULL, total_size_, PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd_, 0));
    if (shm_ptr_ == MAP_FAILED) {
        printf("ShmCreator: mmap failed, shm_fd_ = %d\n", shm_fd_);
        return false;
    }
    return true;
}

template<typename T>
bool ShmCreator<T>::open(bool create) {
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

template<typename T>
void ShmCreator<T>::delete_shm() {
    if (is_owner_) {
        close();
        shm_unlink(shm_name_.c_str());
    }
}

template<typename T>
void ShmCreator<T>::close() {
    if (shm_ptr_ && shm_ptr_ != MAP_FAILED) {
        munmap(shm_ptr_, total_size_);
        shm_ptr_ = NULL;
    }
    if (shm_fd_ >= 0) {
        ::close(shm_fd_);
        shm_fd_ = -1;
    }
}

template<typename T>
bool ShmCreator<T>::valid() const {
    return shm_ptr_ && shm_ptr_ != MAP_FAILED;
}

template<typename T>
std::string ShmCreator<T>::get_shm_name() {
    return shm_name_;
}


} // namespace MulProcess
} // namespace IpcInterface
