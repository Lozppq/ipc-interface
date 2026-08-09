/**
 * @file process_2.cpp
 * @brief process_2 demo：初始化后周期性向 process_1 发消息
 */
#include "mul_process/ShmManager.h"
#include "define/Common.h"
#include "define/MessageId.h"
#include "log/Log_Print.h"
#include <cstdio>
#include <cstring>
#include <vector>
#include <unistd.h>

int main() {
    IpcInterface::Log::setLogPrefix("process_2");
    auto* mgr = IpcInterface::MulProcess::ShmManager::getInstance();
    mgr->initParams(IpcInterface::Define::Process2);
    mgr->start();

    LOG_INFO("process_2 started, pid=%d", getpid());

    int seq = 0;
    while (true) {
        char text[128];
        std::snprintf(text, sizeof(text), "hello from process_2 #%d", seq++);
        std::vector<uint8_t> msg(text, text + std::strlen(text) + 1);
        mgr->send(std::move(msg), IpcInterface::Define::MESSAGE_ID_PROCESS,
                  IpcInterface::Define::Process1);
        sleep(1);
    }
    return 0;
}
