#include "../mul_process/ShmManager.h"
#include <cstdio>
#include <cstdlib>
#if defined(__linux__)
#include <unistd.h>
#include <sys/wait.h>
#endif

typedef struct {
    std::string process_name;
    std::string process_client_name;
    std::string process_executable_name;
} ProcessInfo;

#if defined(__linux__)
// 启动进程，返回子进程 pid；失败返回 -1
pid_t start_process(const std::string& process_executable_name) {
    pid_t pid = fork();
    if (pid < 0) {
        perror("fork");
        return -1;
    }
    if (pid == 0) {
        execlp(process_executable_name.c_str(), process_executable_name.c_str(), (char*)NULL);
        perror("execlp");
        _exit(127);
    }
    return pid;
}
#endif

int main(int argc, char* argv[]) {
#if defined(__linux__)
    IpcInterface::MulProcess::ShmManager::getInstance().initParams(IpcInterface::Define::Daemon, IpcInterface::Define::StatDaemon);
    ProcessInfo process_info[] = {
        {
            IpcInterface::Define::UI,
            IpcInterface::Define::StatUI,
            "./ui",
        },
        {
            IpcInterface::Define::Worker,
            IpcInterface::Define::StatWorker,
            "./worker",
        }
    };
    for (int i = 0; i < sizeof(process_info) / sizeof(process_info[0]); i++) {
        pid_t pid = start_process(process_info[i].process_executable_name);
        if (pid < 0) {
            perror("start_process");
            return -1;
        }
        IpcInterface::MulProcess::ShmManager::getInstance().addPidNameInfo({process_info[i].process_name, process_info[i].process_client_name, pid});
    }
    IpcInterface::MulProcess::ShmManager::getInstance().start();

    // 在这里使用waitpid监控有没有崩溃的子线程，如果有则做完相应处理后重新启动
    while (true) {
        pid_t pid = waitpid(-1, NULL, 0);
        if (pid > 0) {
            std::string process_name;
            
            // 做完相应处理后重新启动
            pid_t t_pid = 0;
            while (t_pid <= 0) {
                t_pid = start_process(process_info[pid].process_executable_name);
            }
            
        }
    }
    return 0;
#else
    (void)argc;
    (void)argv;
    return 1;
#endif
}
