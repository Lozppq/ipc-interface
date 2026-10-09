/**
 * @file ProcessManager.cpp
 * @brief 进程管理器
*/

#include "ProcessManager.h"
#include "ShmManager.h"
#include "../log/Log_Print.h"
#include <algorithm>
#include <cstring>
#if defined(__linux__)
#include <unistd.h>
#include <cerrno>
#include <sys/syscall.h>
#ifndef SYS_pidfd_open
#ifdef __NR_pidfd_open
#define SYS_pidfd_open __NR_pidfd_open
#endif
#endif
#endif

namespace IpcInterface
{
namespace MulProcess
{

ProcessManager::ProcessManager()
    : MessageThread(1024, "ProcessManager")
{
}

ProcessManager::~ProcessManager()
{
}

ProcessManager* ProcessManager::getInstance()
{
    static ProcessManager instance;
    return &instance;
}

void ProcessManager::OnThreadInit()
{
    initProcessSyncShm();
}

void ProcessManager::postCreateProcess(std::string shm_name)
{
    post([this, shm_name = std::move(shm_name)]()
    {
        const uint8_t logic_id = getLogicProcessId(shm_name);
        if (logic_id >= Define::kShmNameCount)
        {
            LOG_ERROR("ProcessManager: postCreateProcess invalid shm_name: %s", shm_name.c_str());
            return;
        }
        if (!shouldLaunchAtBoot(logic_id))
            return;
        createProcess(shm_name, Define::kProcesses[logic_id].m_executable);
    });
}

void ProcessManager::setProcessStartedCallback(ProcessStartedCallback callback)
{
    m_process_started_callback = std::move(callback);
}

void ProcessManager::createProcess(std::string shm_name, std::string process_executable_name)
{
    if (process_executable_name.empty())
        return;
    if (isAllowCreateProcess(shm_name))
    {
        uint32_t pid = startProcess(process_executable_name);
        if (pid > 0)
        {
            m_process_infos.push_back({shm_name, process_executable_name, pid, -1});
            if (m_process_started_callback)
                m_process_started_callback(shm_name);
            LOG_DEBUG("ProcessManager: create process success, shm_name: %s, pid: %d", shm_name.c_str(), pid);
        }
        else
        {
            LOG_ERROR("ProcessManager: failed, shm_name: %s, process_executable_name: %s, pid: %d",
                shm_name.c_str(), process_executable_name.c_str(), pid);
            postTimer(1000,
                [this, shm_name = std::move(shm_name),
                    process_executable_name = std::move(process_executable_name)](int) mutable
                {
                    createProcess(std::move(shm_name), std::move(process_executable_name));
                });
        }
    }
    else
    {
        LOG_DEBUG("ProcessManager: not allow create process, shm_name: %s", shm_name.c_str());
        postTimer(1000,
            [this, shm_name = std::move(shm_name),
                process_executable_name = std::move(process_executable_name)](int) mutable
            {
                createProcess(std::move(shm_name), std::move(process_executable_name));
            });
    }
}

bool ProcessManager::isAllowCreateProcess(const std::string& shm_name)
{
    if (!m_process_sync_shm_creator || !m_process_sync_shm_creator->get_shm_ptr())
        return false;
    uint32_t fd = Define::INVALID_FD;
    for (uint32_t i = 0; i < Define::kShmNameCount; i++)
    {
        if (shm_name == Define::kProcesses[i].m_shm_name)
        {
            fd = i;
            break;
        }
    }
    if (fd >= Define::kShmNameCount)
        return false;
    return m_process_sync_shm_creator->get_shm_ptr()->m_flags[fd].load(std::memory_order_acquire)
        == Define::PROCESS_SYNC_FLAG_DONE;
}

uint8_t ProcessManager::getLogicProcessId(const std::string& shm_name) const
{
    for (uint32_t i = 0; i < Define::kShmNameCount; i++)
    {
        if (shm_name == Define::kProcesses[i].m_shm_name)
            return static_cast<uint8_t>(i);
    }
    return Define::INVALID_FD;
}

uint8_t ProcessManager::lookupLogicIdByPid(uint32_t os_pid) const
{
    auto it = std::find_if(m_process_infos.begin(), m_process_infos.end(),
        [os_pid](const ProcessInfo& process_info)
        {
            return process_info.m_pid == os_pid;
        });
    if (it == m_process_infos.end())
        return Define::INVALID_FD;
    return getLogicProcessId(it->m_shm_name);
}

bool ProcessManager::isNeedActivePullProcess(uint32_t pid)
{
    auto it = std::find_if(m_process_infos.begin(), m_process_infos.end(),
        [pid](const ProcessInfo& process_info)
        {
            return process_info.m_pid == pid;
        });
    if (it != m_process_infos.end())
        return true;
    return false;
}

uint32_t ProcessManager::startProcess(const std::string& process_executable_name)
{
#if defined(__linux__)
    pid_t pid = fork();
    if (pid < 0)
    {
        perror("fork");
        return 0;
    }
    if (pid == 0)
    {
        execlp(process_executable_name.c_str(), process_executable_name.c_str(), (char*)NULL);
        perror("execlp");
        _exit(127);
    }
    return static_cast<uint32_t>(pid);
#else
    LOG_ERROR("ProcessManager: not supported on this platform, process_executable_name: %s",
        process_executable_name.c_str());
    return 0;
#endif
}

bool ProcessManager::executableOk(const char* exe)
{
    return exe && exe[0] != '\0';
}

bool ProcessManager::shouldLaunchAtBoot(uint8_t logic_id)
{
    if (logic_id >= Define::kShmNameCount)
        return false;
    const Define::ProcessDesc& p = Define::kProcesses[logic_id];
    return p.m_boot && executableOk(p.m_executable);
}

bool ProcessManager::shouldRelaunchAfterOnline(uint8_t logic_id)
{
    if (logic_id >= Define::kShmNameCount)
        return false;
    const Define::ProcessDesc& p = Define::kProcesses[logic_id];
    return !p.m_boot && p.m_restart && executableOk(p.m_executable);
}

void ProcessManager::unwatchPidfd(ProcessInfo& info)
{
    if (info.m_fd < 0)
        return;
    removeFd(info.m_fd);
#if defined(__linux__)
    ::close(info.m_fd);
#endif
    info.m_fd = -1;
}

void ProcessManager::onPidfd(int fd)
{
    auto it = std::find_if(m_process_infos.begin(), m_process_infos.end(),
        [fd](const ProcessInfo& info)
        {
            return info.m_fd == fd;
        });
    if (it == m_process_infos.end())
        return;
    const uint32_t os_pid = it->m_pid;
    const uint8_t logic_id = getLogicProcessId(it->m_shm_name);
    unwatchPidfd(*it);
    ShmManager::getInstance()->post([logic_id, os_pid]()
    {
        ShmManager::getInstance()->handleProcessCrash(logic_id);
        ProcessManager::getInstance()->postHandleProcessCrash(os_pid);
    });
}

void ProcessManager::handleProcessOnline(uint8_t logic_id, uint32_t os_pid)
{
    if (logic_id == Define::Daemon_Fd || logic_id >= Define::kShmNameCount)
        return;
    if (shouldLaunchAtBoot(logic_id))
        return;
    const std::string shm_name = Define::kProcesses[logic_id].m_shm_name;
    for (const auto& info : m_process_infos)
    {
        if (info.m_shm_name == shm_name && info.m_fd < 0)
            return;
    }
    for (auto it = m_process_infos.begin(); it != m_process_infos.end();)
    {
        if (it->m_shm_name != shm_name)
        {
            ++it;
            continue;
        }
        unwatchPidfd(*it);
        it = m_process_infos.erase(it);
    }
#if defined(__linux__)
#ifdef SYS_pidfd_open
    int fd = static_cast<int>(syscall(SYS_pidfd_open, static_cast<pid_t>(os_pid), 0u));
#else
    int fd = -1;
    errno = ENOSYS;
#endif
    if (fd < 0)
    {
        LOG_ERROR("ProcessManager: pidfd_open failed, logic_id=%u pid=%u err=%s",
            logic_id, os_pid, std::strerror(errno));
        return;
    }
    if (!addFd(fd, [this](int watch_fd)
    {
        onPidfd(watch_fd);
    }))
    {
        LOG_ERROR("ProcessManager: addFd pidfd failed, logic_id=%u pid=%u", logic_id, os_pid);
        ::close(fd);
        return;
    }
    std::string exe;
    if (shouldRelaunchAfterOnline(logic_id))
        exe = Define::kProcesses[logic_id].m_executable;
    m_process_infos.push_back({shm_name, std::move(exe), os_pid, fd});
    ShmManager::getInstance()->enableChannel(shm_name);
    LOG_DEBUG("ProcessManager: watch external pid, logic_id=%u pid=%u fd=%d", logic_id, os_pid, fd);
#else
    (void)os_pid;
    (void)shm_name;
#endif
}

void ProcessManager::postProcessOnline(uint8_t logic_id, uint32_t os_pid)
{
    post([this, logic_id, os_pid]()
    {
        handleProcessOnline(logic_id, os_pid);
    });
}

void ProcessManager::handleProcessCrash(uint32_t pid)
{
    auto it = std::find_if(m_process_infos.begin(), m_process_infos.end(),
        [pid](const ProcessInfo& process_info)
        {
            return process_info.m_pid == pid;
        });
    if (it == m_process_infos.end())
    {
        LOG_ERROR("ProcessManager: process not found, pid: %d", pid);
        return;
    }
    const std::string shm_name = std::move(it->m_shm_name);
    const std::string exe = std::move(it->m_process_executable_name);
    unwatchPidfd(*it);
    m_process_infos.erase(it);
    if (!exe.empty())
        createProcess(shm_name, exe);
}

void ProcessManager::postHandleProcessCrash(uint32_t pid)
{
    post([this, pid]()
    {
        handleProcessCrash(pid);
    });
}

void ProcessManager::initProcessSyncShm()
{
    m_process_sync_shm_creator = std::make_shared<Model::ShmCreator<Define::ProcessSyncInfo>>(
        Define::ProcessSyncShmName, sizeof(Define::ProcessSyncInfo));
    if (m_process_sync_shm_creator && m_process_sync_shm_creator->Open(true)
        && m_process_sync_shm_creator->get_shm_ptr())
    {
        LOG_DEBUG("ProcessManager: init process sync shm success, shm_name: %s", Define::ProcessSyncShmName);
        auto process_sync_info = m_process_sync_shm_creator->get_shm_ptr();

        // 暂无全部按已同步处理；后续可按槽位 flags[Daemon_Fd/ProcessN_Fd] 分别置位
        // bootstrap 占位：上述循环非真实 per-slot 握手，仅为启动阶段允许 createProcess
        for (uint32_t i = 0; i < Define::kShmNameCount; i++)
            process_sync_info->m_flags[i].store(Define::kProcesses[i].m_sync_flag, std::memory_order_release);
    }
    else
    {
        LOG_ERROR("ProcessManager: init process sync shm failed, shm_name: %s", Define::ProcessSyncShmName);
        postTimer(1000, [this](int)
        {
            initProcessSyncShm();
        });
    }
}

void ProcessManager::setProcessSyncFlag(uint8_t logic_id, uint8_t flag)
{
    if (!m_process_sync_shm_creator || !m_process_sync_shm_creator->get_shm_ptr()
        || logic_id >= Define::kShmNameCount)
        return;
    m_process_sync_shm_creator->get_shm_ptr()->m_flags[logic_id].store(flag, std::memory_order_release);
    LOG_DEBUG("ProcessManager: set process sync flag success, logic_id: %d, flag: %d", logic_id, flag);
}

} // namespace MulProcess
} // namespace IpcInterface
