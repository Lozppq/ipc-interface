#pragma once

#include "../model/MessageThread.h"
#include "../define/Common.h"
#include "../model/ShmCreator.h"
#include <vector>
#include <memory>
namespace IpcInterface {
namespace MulProcess {

// 进程信息
typedef struct {
    std::string shm_name;  // 共享内存名称
    std::string process_executable_name;  // 进程可执行文件名称
    uint32_t pid;  // 进程id
} ProcessInfo;

class ProcessManager : public Model::MessageThread {
public:
    // 定义一个创建进程以后得回调函数，两个参数分别是shm_name, pid
    using CreateProcessCallback = std::function<void(std::string shm_name, int pid)>;
    ProcessManager();
    ~ProcessManager();

    /**
     * @brief 单例类
     * @return 单例类指针
     */
    static ProcessManager* getInstance();

    ProcessManager(const ProcessManager&) = delete;
    ProcessManager& operator=(const ProcessManager&) = delete;

    /**
     * @brief 创建进程以后得回调函数
     * @param callback 回调函数
     */
    void setCreateProcessCallback(CreateProcessCallback callback);


protected:

    /**
     * @brief 初始化创建所需的所有进程
    */
    void initCreateProcess();

    /**
     * @brief 启动进程函数
     * @param process_executable_name 进程可执行文件名称
     * @return 进程id
     */
    uint32_t startProcess(const std::string& process_executable_name);

    /**
     * @brief 创建进程函数
     * @param shm_name 共享内存名称
     * @param process_executable_name 进程可执行文件名称
     */
    void createProcess(std::string shm_name, std::string process_executable_name);

    /**
     * @brief 处理进程崩溃共享内存的重置
     * @param pid 进程id
    */
    void handleProcessCrash(uint32_t pid);
    
    /**
     * @brief 初始化进程同步信息共享内存
    */
    void initProcessSyncShm();

protected:
    void OnThreadInit() override;


private:
    CreateProcessCallback create_process_callback_;
    std::vector<ProcessInfo> process_infos_;
    // 进程同步信息共享内存
    std::shared_ptr<Model::ShmCreator<Define::ProcessSyncInfo>> process_sync_shm_creator_;
};


} // namespace MulProcess
} // namespace IpcInterface