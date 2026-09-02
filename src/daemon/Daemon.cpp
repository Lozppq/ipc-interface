#include "../mul_process/ShmManager.h"
#include "../mul_process/ProcessManager.h"
#include "../log/Log_Print.h"
#include <cstdio>
#include <cstdlib>
#if defined(__linux__)
#include <unistd.h>
#include <sys/wait.h>
#endif

int main(int argc, char* argv[]) {
#if defined(__linux__)
    IpcInterface::Log::setLogPrefix("daemon");
    IpcInterface::MulProcess::ShmManager::getInstance()->initParams(IpcInterface::Define::Daemon);
    // 子进程拉起后，把 shm_name / 逻辑槽位登记到 ShmManager
    IpcInterface::MulProcess::ProcessManager::getInstance()->setCreateProcessCallback(
        [](std::string shm_name, uint8_t logic_id) {
            IpcInterface::MulProcess::ShmManager::getInstance()->postCreatePidNameInfo(
                {shm_name, IpcInterface::Define::INVALID_FD, logic_id});
        });
    // 设置同步标志回调函数
    IpcInterface::MulProcess::ShmManager::getInstance()->setSyncFlagCallback([](uint8_t logic_id, uint8_t flag) {
        IpcInterface::MulProcess::ProcessManager::getInstance()->setProcessSyncFlag(logic_id, flag);
    });
    IpcInterface::MulProcess::ShmManager::getInstance()->start();
    IpcInterface::MulProcess::ProcessManager::getInstance()->start();

    // 阻塞等待子进程退出；业务进程崩溃后回收 shm 并重新拉起
    while (true) {
        pid_t pid = waitpid(-1, NULL, 0);
        if (pid > 0) {
            LOG_ERROR("Daemon: process exited, pid: %d", pid);
            IpcInterface::MulProcess::ProcessManager::getInstance()->post([pid]() {
                auto process_manager = IpcInterface::MulProcess::ProcessManager::getInstance();
                const uint32_t os_pid = static_cast<uint32_t>(pid);
                if (!process_manager->isNeedActivePullProcess(os_pid)) {
                    return;
                }
                const uint8_t logic_id = process_manager->lookupLogicIdByPid(os_pid);
                IpcInterface::MulProcess::ShmManager::getInstance()->post([os_pid, logic_id]() {
                    IpcInterface::MulProcess::ShmManager::getInstance()->handleProcessCrash(logic_id);
                    IpcInterface::MulProcess::ProcessManager::getInstance()->postHandleProcessCrash(os_pid);
                });
            });
        }
    }
    return 0;
#else
    (void)argc;
    (void)argv;
    return 1;
#endif
}
