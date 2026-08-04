/**
 * @file ShmCreator.h
 * @brief 共享内存创建器
*/

#pragma once

#include <cstdint>
#include <string>
#include <cstdio>
#include "../log/Log_Print.h"
#if defined(__linux__)
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#else
#ifndef MAP_FAILED
#define MAP_FAILED ((void*)-1)
#endif
#endif

namespace IpcInterface {
namespace Model {

template<typename T>
class ShmCreator {
public:
    ShmCreator(const std::string& name, uint32_t total_size);
    ~ShmCreator();
    ShmCreator(const ShmCreator&) = delete;
    ShmCreator& operator=(const ShmCreator&) = delete;

    /**
     * @brief 获取共享内存指针
     * @return 共享内存指针
     */
    T* get_shm_ptr() const;

    /**
     * @brief 打开共享内存
     * @param create true=创建模式，false=仅打开模式
     * @return 成功返回true，失败返回false
     */
    bool open(bool create);
    
     /**
      * @brief 关闭共享内存，释放资源
      */
    void close();
     
     /**
      * @brief 检查是否有效
      * @return 有效返回true，否则返回false
      */
    bool valid() const;
 
     /**
      * @brief 获取共享内存名称
      * @return 共享内存名称
      */
    std::string get_shm_name(); 
     
private:
     /**
      * @brief 创建共享内存结构体
      */
    bool create_shm(bool create);
     
     /**
      * @brief 删除共享内存
      */
    void delete_shm();


protected:
    uint32_t total_size_;
    T* shm_ptr_;
    int shm_fd_;
    bool is_owner_;
    std::string shm_name_;
};

#include "ShmCreator.inl"

} // namespace Model
} // namespace IpcInterface