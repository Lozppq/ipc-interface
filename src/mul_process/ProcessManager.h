#pragma once

#include "../model/MessageThread.h"

namespace IpcInterface {
namespace MulProcess {

class ProcessManager : public Model::MessageThread {
public:
    ProcessManager();
    ~ProcessManager();

    /**
     * @brief 单例类
     * @return 单例类指针
     */
    static ProcessManager* getInstance();

    ProcessManager(const ProcessManager&) = delete;
    ProcessManager& operator=(const ProcessManager&) = delete;

protected:
    void OnThreadInit() override;
};


} // namespace MulProcess
} // namespace IpcInterface