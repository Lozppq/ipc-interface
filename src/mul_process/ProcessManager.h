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
    std::string m_shm_name;  // 共享内存名称
    std::string m_process_executable_name;  // 进程可执行文件名称
    uint32_t m_pid;  // 进程id
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

    /**
     * @brief 获取是否允许创建进程
     * @return 是否允许创建进程
     */
    bool isAllowCreateProcess(const std::string& shm_name);

    /**
     * @brief 判断是否是需要主动拉起的进程
     * @param pid 进程id
     * @return 是否是需要主动拉起的进程
     */
    bool isNeedActivePullProcess(uint32_t pid);

    /**
     * @brief 外部线程投递一次处理进程崩溃共享内存的重置
     * @param pid 进程id
    */
    void postHandleProcessCrash(uint32_t pid);

protected:
    /**
     * @brief 处理进程崩溃共享内存的重置
     * @param pid 进程id
    */
    void handleProcessCrash(uint32_t pid);

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
     * @brief 初始化进程同步信息共享内存
    */
    void initProcessSyncShm();

protected:
    void OnThreadInit() override;


private:
    CreateProcessCallback m_create_process_callback;
    std::vector<ProcessInfo> m_process_infos;
    // 进程同步信息共享内存
    std::shared_ptr<Model::ShmCreator<Define::ProcessSyncInfo>> m_process_sync_shm_creator;
};


} // namespace MulProcess
} // namespace IpcInterface