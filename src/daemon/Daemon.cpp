#include "../mul_process/ShmManager.h"
#include "../mul_process/ProcessManager.h"
#include "../log/Log_Print.h"
#include <cstdio>
#include <cstdlib>
#if defined(__linux__)
#include <unistd.h>
#include <sys/wait.h>
#endif

static void onProcessCrash(uint8_t logic_id, uint32_t os_pid)
{
    IpcInterface::MulProcess::ShmManager::getInstance()->post([logic_id, os_pid]()
    {
        IpcInterface::MulProcess::ShmManager::getInstance()->handleProcessCrash(logic_id);
        IpcInterface::MulProcess::ProcessManager::getInstance()->postHandleProcessCrash(os_pid);
    });
}

int main(int argc, char* argv[])
{
    IpcInterface::Log::setLogPrefix("daemon");
    IpcInterface::MulProcess::ShmManager::getInstance()->initParams(IpcInterface::Define::Daemon);
    IpcInterface::MulProcess::ShmManager::getInstance()->setStartProcessCallback(
        [](std::string shm_name, uint8_t)
        {
            IpcInterface::MulProcess::ProcessManager::getInstance()->postCreateProcess(std::move(shm_name));
        });
    IpcInterface::MulProcess::ShmManager::getInstance()->setSyncFlagCallback(
        [](uint8_t logic_id, uint8_t flag)
        {
            IpcInterface::MulProcess::ProcessManager::getInstance()->setProcessSyncFlag(logic_id, flag);
        });
    IpcInterface::MulProcess::ShmManager::getInstance()->setProcessOnlineCallback(
        [](uint8_t logic_id, uint32_t os_pid)
        {
            IpcInterface::MulProcess::ProcessManager::getInstance()->postProcessOnline(logic_id, os_pid);
        });
    IpcInterface::MulProcess::ProcessManager::getInstance()->setProcessStartedCallback(
        [](std::string shm_name)
        {
            IpcInterface::MulProcess::ShmManager::getInstance()->enableChannel(std::move(shm_name));
        });
    IpcInterface::MulProcess::ProcessManager::getInstance()->setProcessCrashCallback(onProcessCrash);
    IpcInterface::MulProcess::ProcessManager::getInstance()->start();
    IpcInterface::MulProcess::ShmManager::getInstance()->start();

#if defined(__linux__)
    while (true)
    {
        pid_t pid = waitpid(-1, NULL, 0);
        if (pid > 0)
        {
            LOG_ERROR("Daemon: process exited, pid: %d", pid);
            IpcInterface::MulProcess::ProcessManager::getInstance()->post([pid]()
            {
                auto process_manager = IpcInterface::MulProcess::ProcessManager::getInstance();
                const uint32_t os_pid = static_cast<uint32_t>(pid);
                if (!process_manager->isNeedActivePullProcess(os_pid))
                    return;
                onProcessCrash(process_manager->lookupLogicIdByPid(os_pid), os_pid);
            });
        }
    }
#else
    (void)argc;
    (void)argv;
    return 1;
#endif
}
