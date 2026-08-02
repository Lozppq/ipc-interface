#include "../mul_process/ShmManager.h"
#include <cstdio>
#include <cstdlib>
#if defined(__linux__)
#include <unistd.h>
#include <sys/wait.h>
#endif

int main(int argc, char* argv[]) {
#if defined(__linux__)
    IpcInterface::MulProcess::ShmManager::getInstance()->initParams(IpcInterface::Define::Daemon, IpcInterface::Define::StatDaemon);
    // 设置创建进程回调函数，在里面调用IpcInterface::MulProcess::ShmManager::getInstance()->postCreatePidNameInfo函数
    IpcInterface::MulProcess::ProcessManager::getInstance()->setCreateProcessCallback([](std::string shm_name, std::string client_name, uint32_t pid) {
        IpcInterface::MulProcess::ShmManager::getInstance()->postCreatePidNameInfo({shm_name, client_name, pid});
    });
    IpcInterface::MulProcess::ShmManager::getInstance()->start();
    IpcInterface::MulProcess::ProcessManager::getInstance()->start();

    // 在这里使用waitpid监控有没有崩溃的子线程，如果有则做完相应处理后重新启动
    while (true) {
        pid_t pid = waitpid(-1, NULL, 0);
        if (pid > 0) {
            LOG_ERROR("Daemon: process crashed, pid: %d", pid);
            // 这里先post到ShmManager的handleProcessCrash函数
            IpcInterface::MulProcess::ShmManager::getInstance()->post([pid]() {
                IpcInterface::MulProcess::ShmManager::getInstance()->handleProcessCrash(pid);
                IpcInterface::MulProcess::ProcessManager::getInstance()->post([pid](){
                    IpcInterface::MulProcess::ProcessManager::getInstance()->handleProcessCrash(pid);
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
