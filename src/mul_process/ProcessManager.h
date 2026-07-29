#pragma once

#include "../model/MessageThread.h"

namespace IpcInterface {
namespace MulProcess {

class ProcessManager : public Model::MessageThread {
public:
    ProcessManager();
    ~ProcessManager();

protected:
    void OnThreadInit() override;
};


} // namespace MulProcess
} // namespace IpcInterface