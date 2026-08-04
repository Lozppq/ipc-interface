/**
 * @file ProcessManager.cpp
 * @brief 进程管理器
*/


#include "ProcessManager.h"
#include "../log/Log_Print.h"

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
    for (int i = 0; i < Define::kShmNameCount; i++) {
        createProcess(Define::kShmNames[i], Define::kProcessExecutableNames[i]);
    }
}

void ProcessManager::createProcess(std::string shm_name, std::string process_executable_name){
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
    if (process_sync_shm_creator_ && process_sync_shm_creator_->get_shm_ptr() && process_sync_shm_creator_->open(true)) {
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