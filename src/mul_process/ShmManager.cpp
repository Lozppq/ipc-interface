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
    addPidNameInfo(PidNameInfo{.name = shm_name, .pid = getpid()});
}

ShmManager::~ShmManager() {

}


ShmManager& ShmManager::getInstance() {
    static ShmManager instance;
    return instance;
}

void ShmManager::addPidNameInfo(PidNameInfo info){
    pidNameInfos_.push_back(info);
}

void ShmManager::OnThreadInit() {
    initShm(shm_name_ == Define::Daemon);
    initClientStatusInfo(shm_name_ == Define::Daemon);
    initReceiveWork();
    initSendWork();
}

void ShmManager::initShm(bool create) {
    for (auto& info : pidNameInfos_) {
        shmInfosMap_.emplace(info.name, std::make_unique<StreamShmCreator>(info.name));
        openStreamShmRetry(info, create);
    }
}

void ShmManager::openStreamShmRetry(PidNameInfo info, bool create) {
    auto& shm = *shmInfosMap_.at(info.name);
    if (shm.open(create)) {
        shm.set_receiver_pid(info.pid);
        shm.set_senders_pid(0);
        printf("ShmManager: initShm success, name = %s, pid = %d\n", info.name.c_str(), info.pid);
    } else {
        printf("ShmManager: initShm failed, name = %s, pid = %d\n", info.name.c_str(), info.pid);
        shm.close();
        postTimer(100, [this, info, create]() {
            openStreamShmRetry(info, create);
        });
    }
}

void ShmManager::initClientStatusInfo(bool create) {
    if (create) {
        for (auto& info : pidNameInfos_) {
            clientStatusInfosMap_.emplace(
                info.name,
                std::make_unique<ShmCreator<ClientStatusInfo>>(info.name, sizeof(ClientStatusInfo)));
            openClientStatusInfoRetry(info);
        }
    }
}

void ShmManager::openClientStatusInfoRetry(PidNameInfo info) {
    auto& shm = *clientStatusInfosMap_.at(info.name);
    if (shm.open(true)) {
        if (auto* p = shm.get_shm_ptr()) {
            p->local_pid.store(info.pid, std::memory_order_relaxed);
        }
        printf("ShmManager: initClientStatusInfo success, name = %s, pid = %d\n", info.name.c_str(), info.pid);
    } else {
        printf("ShmManager: initClientStatusInfo failed, name = %s, pid = %d\n", info.name.c_str(), info.pid);
        shm.close();
        postTimer(100, [this, info]() {
            openClientStatusInfoRetry(info);
        });
    }
}

void ShmManager::initReceiveWork() {
    receive_work_ = new ReceiveWork(shmInfosMap_.at(shm_name_), [this](std::shared_ptr<TagReceiveMessage> tag) {
        onReceiveMessage(tag);
    });
    receive_work_->start();
}


void ShmManager::initSendWork() {
    send_work_ = new SendWork();
    send_work_->start();
}

void ShmManager::send(std::vector<uint8_t>& msg, std::string shm_name) {
    if (!send_work_) {
        return;
    }
    auto it = shmInfosMap_.find(shm_name);
    if (it == shmInfosMap_.end() || !it->second) {
        return;
    }
    send_work_->send(msg, it->second.get());
}

} // namespace MulProcess
} // namespace IpcInterface
