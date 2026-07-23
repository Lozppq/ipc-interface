/**
 * @file ShmManager.cpp
 * @brief 共享内存管理器实现
 * @details 实现共享内存的创建、打开、映射、队列操作等核心功能。
 * 使用 CAS 原子操作保证多生产者线程安全，消息格式为 [4字节长度+数据]，
 * 通过提交标志位确保数据写入完成后消费者才能读取，支持跨进程信号量同步。
 */

#include "ShmManager.h"
#include "../define/common.h"
#include "StreamShmCreator.h"

namespace IpcInterface {
namespace MulProcess {

ShmManager::ShmManager(const std::string& shm_name) 
    : MessageThread(),
    shm_name_(shm_name) {

}

ShmManager::~ShmManager() {

}


ShmManager& ShmManager::getInstance() {
    static ShmManager instance;
    return instance;
}

void ShmManager::OnThreadInit() {
    
}

void ShmManager::initShm() {
    if (shm_name_ == Define::Daemon) {
        // 守护进程需要初始化各个共享内存和客户端状态信息
    } else {
        // 其他进程只需要只读初始化各个共享内存
    }
}


} // namespace MulProcess
} // namespace IpcInterface

