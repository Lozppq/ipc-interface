/**
 * @file ui.cpp
 * @brief UI 进程 demo：初始化后周期性向 Worker 发消息，并接收发往本进程队列的消息
 */
#include "mul_process/ShmManager.h"
#include "define/Common.h"
#include "log/Log_Print.h"
#include <cstdio>
#include <cstring>
#include <vector>
#include <unistd.h>

int main() {
    auto* mgr = IpcInterface::MulProcess::ShmManager::getInstance();
    mgr->initParams(IpcInterface::Define::UI);
    mgr->start();

    LOG_INFO("ui started, pid=%d", getpid());

    int seq = 0;
    while (true) {
        char text[128];
        std::snprintf(text, sizeof(text), "hello from ui #%d", seq++);
        std::vector<uint8_t> msg(text, text + std::strlen(text) + 1);
        mgr->send(msg, IpcInterface::Define::Worker);
        sleep(1);
    }
    return 0;
}
