/**
 * @file process_1.cpp
 * @brief process_1 demo：初始化后周期性向 process_2 发消息
 */
#include "mul_process/ShmManager.h"
#include "define/Common.h"
#include "log/Log_Print.h"
#include <cstdio>
#include <cstring>
#include <vector>
#include <unistd.h>

int main() {
    IpcInterface::Log::setLogPrefix("process_1");
    auto* mgr = IpcInterface::MulProcess::ShmManager::getInstance();
    mgr->initParams(IpcInterface::Define::Process1);
    mgr->start();

    LOG_INFO("process_1 started, pid=%d", getpid());

    int seq = 0;
    while (true) {
        char text[128];
        std::snprintf(text, sizeof(text), "hello from process_1 #%d", seq++);
        std::vector<uint8_t> msg(text, text + std::strlen(text) + 1);
        mgr->send(std::move(msg), IpcInterface::Define::Process2);
        sleep(1);
    }
    return 0;
}
