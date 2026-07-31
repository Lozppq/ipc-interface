/**
 * @file ProcessManager.cpp
 * @brief 进程管理器
*/


#include "ProcessManager.h"

namespace IpcInterface {
namespace MulProcess {

ProcessManager::ProcessManager() 
    : MessageThread() {

}

ProcessManager::~ProcessManager() {

}

ProcessManager* ProcessManager::getInstance() {
    static ProcessManager instance;
    return &instance;
}

void ProcessManager::OnThreadInit() {

}

} // namespace MulProcess
} // namespace IpcInterface