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
#if defined(__linux__)
#include <unistd.h>
#endif

namespace IpcInterface {
namespace MulProcess {

ShmManager::ShmManager() 
    : MessageThread(),
    shm_name_(""),
    client_name_("") {
}

ShmManager::~ShmManager() {
    if (receive_work_) {
        receive_work_->stop();
        delete receive_work_;
    }
    if (send_work_) {
        send_work_->stop();
        delete send_work_;
    }
}

void ShmManager::initParams(const std::string& shm_name, const std::string& client_name) {
    shm_name_ = shm_name;
    client_name_ = client_name;
#if defined(__linux__)
    uint32_t pid = static_cast<uint32_t>(getpid());
#else
    uint32_t pid = 0;
#endif
    if (shm_name_ == Define::Daemon) {
        addPidNameInfo({shm_name, client_name, pid});
    } else { 
        // 不是守护进程，初始化添加所有进程，先默认使用当前进程pid，实则除了守护进程会使用这个pid，其他的进程一般不会使用，共享内存里面有一份就足够了
        for (int i = 0; i < Define::kShmNameCount; i++) {
            addPidNameInfo({Define::kShmNames[i], Define::kStatShmNames[i], pid});
        }
    }
}


ShmManager* ShmManager::getInstance() {
    static ShmManager instance;
    return &instance;
}

void ShmManager::addPidNameInfo(PidNameInfo info){
    pidNameInfos_.push_back(info);
}

void ShmManager::OnThreadInit() {
    initShm(shm_name_ == Define::Daemon);
    initClientStatusInfo(shm_name_ == Define::Daemon);
    initShmClientStatusInfo();
    initReceiveWork();
    initSendWork();
}

void ShmManager::initShm(bool create) {
    for (auto& info : pidNameInfos_) {
        shmInfosMap_.emplace(info.shm_name, std::make_unique<StreamShmCreator>(info.shm_name));
        openStreamShmRetry(info, create);
    }
}

void ShmManager::openStreamShmRetry(PidNameInfo info, bool create) {
    auto& shm = *shmInfosMap_.at(info.shm_name);
    if (shm.open(create)) {
        if (create) {
            shm.set_receiver_pid(info.pid);
            shm.set_senders_pid(0);
        } else {
            // 这里需要更新 pidNameInfos_ 中的 pid
            auto it_info = std::find_if(pidNameInfos_.begin(), pidNameInfos_.end(), [info](const PidNameInfo& item) {
                return item.shm_name == info.shm_name;
            });
            if (it_info != pidNameInfos_.end()) {
                it_info->pid = shm.get_receiver_pid();
            }
        }
        printf("ShmManager: initShm success, name = %s, pid = %d\n", info.shm_name.c_str(), info.pid);
    } else {
        printf("ShmManager: initShm failed, name = %s, pid = %d\n", info.shm_name.c_str(), info.pid);
        shm.close();
        postTimer(100, [this, info, create]() {
            openStreamShmRetry(info, create);
        });
    }
}

void ShmManager::initClientStatusInfo(bool create) {
    for (auto& info : pidNameInfos_) {
        clientStatusInfosMap_.emplace(
            info.shm_name,
            std::make_unique<Model::ShmCreator<ClientStatusInfo>>(info.client_name, sizeof(ClientStatusInfo)));
        openClientStatusInfoRetry(info, create);
    }
}

void ShmManager::openClientStatusInfoRetry(PidNameInfo info, bool create) {
    auto& shm = *clientStatusInfosMap_.at(info.shm_name);
    if (shm.open(create)) {
        if (create) {
            auto* p = shm.get_shm_ptr();
            p->local_pid.store(info.pid, std::memory_order_release);
        }
        printf("ShmManager: initClientStatusInfo success, name = %s, pid = %d\n", info.shm_name.c_str(), info.pid);
    } else {
        printf("ShmManager: initClientStatusInfo failed, name = %s, pid = %d\n", info.shm_name.c_str(), info.pid);
        shm.close();
        postTimer(100, [this, info, create]() {
            openClientStatusInfoRetry(info, create);
        });
    }
}

void ShmManager::initShmClientStatusInfo() {
    for (auto& info : pidNameInfos_) {
        setShmClientStatusInfo(info);
    }
}

void ShmManager::setShmClientStatusInfo(PidNameInfo info) {
    // 先找到当前进程的client信息，然后设置到共享内存中，找不到则需要继续post等待下一次找到在设置进去
    auto it_local_client = clientStatusInfosMap_.find(shm_name_);
    auto it_shm = shmInfosMap_.find(info.shm_name);
    // 如果没找到，或者找到了，但是该client守护进程还未创建完成导致共享内存是无效的，则需要继续post等待下一次找到在设置进去
    if (it_local_client == clientStatusInfosMap_.end() || !it_local_client->second || !it_local_client->second->valid() 
        || it_shm == shmInfosMap_.end() || !it_shm->second || !it_shm->second->valid()) {
        postTimer(100, [this, info]() {
            setShmClientStatusInfo(info);
        });
        return;    
    }
    it_shm->second->set_client_info(it_local_client->second->get_shm_ptr());
}

void ShmManager::initReceiveWork() {
    receive_work_ = new ReceiveWork(shmInfosMap_.at(shm_name_).get(), [this](std::shared_ptr<TagReceiveMessage> tag) {
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
