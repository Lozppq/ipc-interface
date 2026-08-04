/**
 * @file ShmManager.cpp
 * @brief 共享内存管理器实现
 * @details 实现共享内存的创建、打开、映射、队列操作等核心功能。
 * 使用 CAS 原子操作保证多生产者线程安全，消息格式为 [4字节长度+数据]，
 * 通过提交标志位确保数据写入完成后消费者才能读取，支持跨进程信号量同步。
 */

#include "ShmManager.h"
#include "../define/Common.h"
#include "StreamShmCreator.h"
#if defined(__linux__)
#include <unistd.h>
#endif

namespace IpcInterface {
namespace MulProcess {

ShmManager::ShmManager() 
    : MessageThread(),
    shm_name_("") {
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

void ShmManager::initParams(const std::string& shm_name) {
    shm_name_ = shm_name;
#if defined(__linux__)
    uint32_t pid = static_cast<uint32_t>(getpid());
#else
    uint32_t pid = 0;
#endif
    if (shm_name_ == Define::Daemon) {
        addPidNameInfo({shm_name, pid});
    } else { 
        // 不是守护进程，初始化添加所有进程，先默认使用当前进程pid，实则除了守护进程会使用这个pid，其他的进程一般不会使用，共享内存里面有一份就足够了
        for (int i = 0; i < Define::kShmNameCount; i++) {
            addPidNameInfo({Define::kShmNames[i], pid});
        }
    }
}


ShmManager* ShmManager::getInstance() {
    static ShmManager instance;
    return &instance;
}

void ShmManager::addPidNameInfo(PidNameInfo info){
    // 如果在当前线程直接添加，否则需要post等待下一次找到在添加进去
    if (isInWorkerThread()) {
        pidNameInfos_.push_back(info);
    } else {
        post([this, info]() {
            pidNameInfos_.push_back(info);
        });
    }
}

void ShmManager::postCreatePidNameInfo(PidNameInfo info) {
    post([this, info]() {
        auto it = std::find_if(pidNameInfos_.begin(), pidNameInfos_.end(),
            [&info](const PidNameInfo& item) {
                return item.shm_name == info.shm_name;
        });
        if (it == pidNameInfos_.end()) {
            pidNameInfos_.push_back(info);
        } else if (it->pid == info.pid) { // 如果pid相同，则直接返回
            return;
        }
        // 这里代表需要更新pid或者需要添加新的pidNameInfo
        auto it_shm = shmInfosMap_.find(info.shm_name);
        if (it_shm == shmInfosMap_.end()) {
            shmInfosMap_.emplace(info.shm_name, std::make_unique<StreamShmCreator>(info.shm_name));
            openStreamShmRetry(info, true);
        } else {
            // 这里更新一下pid，并且允许接收和发送
            it_shm->second->set_receiver_pid(info.pid);
            it_shm->second->set_flag(Define::BIT0 | Define::BIT1);
        }

    });
}

void ShmManager::OnThreadInit() {
    initShm(shm_name_ == Define::Daemon);
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
    // 如果共享内存已经打开，则直接返回
    if (shm.valid()) {
        return;
    }
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
        LOG_INFO("ShmManager: openStreamShmRetry success, name = %s, pid = %d", info.shm_name.c_str(), info.pid);
    } else {
        LOG_ERROR("ShmManager: openStreamShmRetry failed, name = %s, pid = %d", info.shm_name.c_str(), info.pid);
        shm.close();
        postTimer(100, [this, info, create]() {
            openStreamShmRetry(info, create);
        });
    }
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

void ShmManager::handleProcessCrash(uint32_t pid) {
    // 先找到pidNameInfos_中pid对应的PidNameInfo，如果是进程通信的shm只需要重置，其他的临时通道需要直接删除
    for (int i = 0; i < pidNameInfos_.size(); ) {
        if (pidNameInfos_[i].pid == pid) {
            uint8_t logic_process_id = getLogicProcessId(pidNameInfos_[i].shm_name);
            auto it_shm = shmInfosMap_.find(pidNameInfos_[i].shm_name);
            if (it_shm != shmInfosMap_.end() && it_shm->second) {
                if (logic_process_id != Define::INVALID_FD) {
                    // 先设置无法发送和接收
                    it_shm->second->set_flag(0);
                    
                    i++;
                } else {
                    // 删除临时共享内存
                    it_shm->second->delete_shm();
                    
                    shmInfosMap_.erase(it_shm);
                    pidNameInfos_.erase(pidNameInfos_.begin() + i);
                }
            } else {
                i++;
            }
        } else {
            i++;
        }
    }
}

uint8_t ShmManager::getLogicProcessId(const std::string& shm_name) {
    for (int i = 0; i < Define::kShmNameCount; i++) {
        if (shm_name == Define::kShmNames[i]) {
            return i;
        }
    }
    return Define::INVALID_FD;
}

} // namespace MulProcess
} // namespace IpcInterface
