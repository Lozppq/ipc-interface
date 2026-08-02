/**
 * @file ProcessManager.cpp
 * @brief 进程管理器
*/


#include "ProcessManager.h"

namespace IpcInterface {
namespace MulProcess {

ProcessManager::ProcessManager(const std::string& process_name) 
    : MessageThread(),
    process_name_(process_name) {

}

ProcessManager::~ProcessManager() {

}

ProcessManager* ProcessManager::getInstance() {
    static ProcessManager instance;
    return &instance;
}

void ProcessManager::OnThreadInit() {
    initCreateProcess();
}

void ProcessManager::setCreateProcessCallback(CreateProcessCallback callback) {
    create_process_callback_ = callback;
}

void ProcessManager::initCreateProcess() {
    for (int i = 0; i < Define::kShmNameCount; i++) {
        createProcess(Define::kShmNames[i], Define::kClientNames[i], Define::kProcessExecutableNames[i]);
    }
}

void ProcessManager::createProcess(std::string shm_name, std::string client_name, std::string process_executable_name){
    uint32_t pid = startProcess(process_executable_name);
    if (pid > 0) {
        process_infos_.push_back({shm_name, client_name, process_executable_name, pid});
        if (create_process_callback_) {
            create_process_callback_(shm_name, client_name, pid);
        }
        LOG_INFO("ProcessManager: create process success, shm_name: %s, client_name: %s, pid: %d", shm_name.c_str(), client_name.c_str(), pid);
    } else {
        LOG_ERROR("ProcessManager: failed, shm_name: %s, client_name: %s, process_executable_name: %s, pid: %d", shm_name.c_str(), client_name.c_str(), process_executable_name.c_str(), pid);
        postTimer(1000, [this, shm_name, client_name, process_executable_name]() {
            createProcess(shm_name, client_name, process_executable_name);
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
        std::string client_name = std::move(it->client_name);
        std::string process_executable_name = std::move(it->process_executable_name);
        process_infos_.erase(it);
        createProcess(shm_name, client_name, process_executable_name);
    } else {
        LOG_ERROR("ProcessManager: process not found, pid: %d", pid);
    }

}

} // namespace MulProcess
} // namespace IpcInterface