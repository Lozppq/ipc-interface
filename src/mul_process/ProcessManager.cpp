/**
 * @file ProcessManager.cpp
 * @brief 进程管理器
*/


#include "ProcessManager.h"
#include "../log/Log_Print.h"
#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#if defined(__linux__)
#include <unistd.h>
#endif

namespace IpcInterface {
namespace MulProcess {

ProcessManager::ProcessManager() 
    : MessageThread(){

}

ProcessManager::~ProcessManager() {

}

ProcessManager* ProcessManager::getInstance() {
    static ProcessManager instance;
    return &instance;
}

void ProcessManager::OnThreadInit() {
    initProcessSyncShm();
    initCreateProcess();
}

void ProcessManager::setCreateProcessCallback(CreateProcessCallback callback) {
    create_process_callback_ = callback;
}

void ProcessManager::initCreateProcess() {
    // 不拉起 daemon 自身，只拉起业务子进程（ui/worker/...）
    for (int i = 0; i < Define::kShmNameCount; i++) {
        if (Define::kShmNames[i] == Define::Daemon) {
            continue;
        }
        createProcess(Define::kShmNames[i], Define::kProcessExecutableNames[i]);
    }
}

void ProcessManager::createProcess(std::string shm_name, std::string process_executable_name){
    if (isAllowCreateProcess(shm_name)) {
        uint32_t pid = startProcess(process_executable_name);
        if (pid > 0) {
            process_infos_.push_back({shm_name, process_executable_name, pid});
            if (create_process_callback_) {
                create_process_callback_(shm_name, pid);
            }
            LOG_INFO("ProcessManager: create process success, shm_name: %s, pid: %d", shm_name.c_str(), pid);
        } else {
            LOG_ERROR("ProcessManager: failed, shm_name: %s, process_executable_name: %s, pid: %d", shm_name.c_str(), process_executable_name.c_str(), pid);
            postTimer(100, [this, shm_name, process_executable_name]() {
                createProcess(shm_name, process_executable_name);
            });
        }
    } else {
        LOG_DEBUG("ProcessManager: not allow create process, shm_name: %s", shm_name.c_str());
        postTimer(100, [this, shm_name, process_executable_name]() {
            createProcess(shm_name, process_executable_name);
        });
    }
}

bool ProcessManager::isAllowCreateProcess(std::string shm_name) {
    if (!process_sync_shm_creator_ || !process_sync_shm_creator_->get_shm_ptr()) {
        return false;
    }
    if (shm_name == Define::Daemon) {
        return process_sync_shm_creator_->get_shm_ptr()->daemon_sync_flag.load(std::memory_order_acquire) == Define::PROCESS_SYNC_FLAG_DONE;
    } else if (shm_name == Define::UI) {
        return process_sync_shm_creator_->get_shm_ptr()->ui_sync_flag.load(std::memory_order_acquire) == Define::PROCESS_SYNC_FLAG_DONE;
    } else if (shm_name == Define::Worker) {
        return process_sync_shm_creator_->get_shm_ptr()->worker_sync_flag.load(std::memory_order_acquire) == Define::PROCESS_SYNC_FLAG_DONE;
    }
    return false;
}

bool ProcessManager::isNeedActivePullProcess(uint32_t pid) {
    auto it = std::find_if(process_infos_.begin(), process_infos_.end(), [pid](const ProcessInfo& process_info) {
        return process_info.pid == pid;
    });
    if (it != process_infos_.end()) {
        return true;
    }
    return false;
}

uint32_t ProcessManager::startProcess(const std::string& process_executable_name) {
#if defined(__linux__)
    pid_t pid = fork();
    if (pid < 0) {
        perror("fork");
        return 0;
    }
    if (pid == 0) {
        execlp(process_executable_name.c_str(), process_executable_name.c_str(), (char*)NULL);
        perror("execlp");
        _exit(127);
    }
    return static_cast<uint32_t>(pid);
#else
    LOG_ERROR("ProcessManager: not supported on this platform, process_executable_name: %s", process_executable_name.c_str());
    return 0;
#endif
}

void ProcessManager::handleProcessCrash(uint32_t pid) {
    // 先找到process_infos_中pid对应的ProcessInfo
    auto it = std::find_if(process_infos_.begin(), process_infos_.end(), [pid](const ProcessInfo& process_info) {
        return process_info.pid == pid;
    });
    if (it != process_infos_.end()) {
        // 如果找到，则重新创建进程，并把原来的记录删除
        std::string shm_name = std::move(it->shm_name);
        std::string process_executable_name = std::move(it->process_executable_name);
        process_infos_.erase(it);
        createProcess(shm_name, process_executable_name);
    } else {
        LOG_ERROR("ProcessManager: process not found, pid: %d", pid);
    }

}

void ProcessManager::initProcessSyncShm() {
    process_sync_shm_creator_ = std::make_shared<Model::ShmCreator<Define::ProcessSyncInfo>>(Define::ProcessSyncShmName, sizeof(Define::ProcessSyncInfo));
    if (process_sync_shm_creator_ && process_sync_shm_creator_->open(true) && process_sync_shm_creator_->get_shm_ptr()) {
        LOG_INFO("ProcessManager: init process sync shm success, shm_name: %s", Define::ProcessSyncShmName);
        auto process_sync_info = process_sync_shm_creator_->get_shm_ptr();

        // 这里没有特殊要求全部按照已同步处理
        process_sync_info->daemon_sync_flag.store(Define::PROCESS_SYNC_FLAG_DONE);
        process_sync_info->ui_sync_flag.store(Define::PROCESS_SYNC_FLAG_DONE);
        process_sync_info->worker_sync_flag.store(Define::PROCESS_SYNC_FLAG_DONE);
    } else {
        LOG_ERROR("ProcessManager: init process sync shm failed, shm_name: %s", Define::ProcessSyncShmName);
        postTimer(100, [this]() {
            initProcessSyncShm();
        });
    }
}

} // namespace MulProcess
} // namespace IpcInterface