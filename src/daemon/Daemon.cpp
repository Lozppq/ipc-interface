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
    // 子进程拉起后，把 shm_name/pid 登记到 ShmManager
    IpcInterface::MulProcess::ProcessManager::getInstance()->setCreateProcessCallback(
        [](std::string shm_name, int pid) {
            // 固定 inbox：多发送者(0) -> 子进程为接收者
            IpcInterface::MulProcess::ShmManager::getInstance()->postCreatePidNameInfo(
                {shm_name, 0, static_cast<uint32_t>(pid)});
        });
    IpcInterface::MulProcess::ShmManager::getInstance()->start();
    IpcInterface::MulProcess::ProcessManager::getInstance()->start();

    // 阻塞等待子进程退出；业务进程崩溃后回收 shm 并重新拉起
    while (true) {
        pid_t pid = waitpid(-1, NULL, 0);
        if (pid > 0) {
            LOG_ERROR("Daemon: process exited, pid: %d", pid);
            IpcInterface::MulProcess::ProcessManager::getInstance()->post([pid]() {
                if (!IpcInterface::MulProcess::ProcessManager::getInstance()->isNeedActivePullProcess(static_cast<uint32_t>(pid))) {
                    return;
                }
                IpcInterface::MulProcess::ShmManager::getInstance()->post([pid]() {
                    IpcInterface::MulProcess::ShmManager::getInstance()->handleProcessCrash(static_cast<uint32_t>(pid));
                    IpcInterface::MulProcess::ProcessManager::getInstance()->postHandleProcessCrash(static_cast<uint32_t>(pid));
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
