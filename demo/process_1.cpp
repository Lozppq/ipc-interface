/**
 * @file process_1.cpp
 * @brief process_1 demo：初始化后周期性向 process_2 发消息
 */
#include "mul_process/ShmManager.h"
#include "define/Common.h"
#include "define/MessageId.h"
#include "log/Log_Print.h"
#include <atomic>
#include <cstdint>
#include <random>
#include <thread>
#include <vector>
#include <unistd.h>

int main() {
    IpcInterface::Log::setLogPrefix("process_1");
    auto* mgr = IpcInterface::MulProcess::ShmManager::getInstance();
    mgr->initParams(IpcInterface::Define::Process1);
    mgr->start();

    std::atomic<uint64_t> recv_bytes{0};
    std::thread([&recv_bytes]() {
        while (true) {
            sleep(1);
            const uint64_t bytes = recv_bytes.exchange(0, std::memory_order_relaxed);
            LOG_INFO("recv rate=%.3f MB/s", bytes / (1024.0 * 1024.0));
        }
    }).detach();

    mgr->setReceiveHandler([&recv_bytes](std::shared_ptr<IpcInterface::MulProcess::TagReceiveMessage> tag) {
        if (!tag) return;
        const auto& data = tag->m_data;
        recv_bytes.fetch_add(data.size(), std::memory_order_relaxed);
        if (data.size() < sizeof(uint16_t) || (data.size() % sizeof(uint16_t)) != 0) {
            LOG_ERROR("recv bad size=%zu message_id=%u", data.size(), tag->m_message_id);
            return;
        }
        const auto* p = reinterpret_cast<const uint16_t*>(data.data());
        const uint16_t n = p[0];
        const size_t expect = (1u + n) * sizeof(uint16_t);
        if (n < 1 || n > 1000 || data.size() != expect) {
            LOG_ERROR("recv bad n=%u size=%zu expect=%zu message_id=%u",
                      n, data.size(), expect, tag->m_message_id);
            return;
        }
        for (uint16_t i = 1; i <= n; ++i) {
            if (p[i] != i) {
                LOG_ERROR("recv bad seq at i=%u got=%u n=%u message_id=%u",
                          i, p[i], n, tag->m_message_id);
                return;
            }
        }
    });

    LOG_INFO("process_1 started, pid=%d", getpid());

    std::mt19937 rng{std::random_device{}()};
    std::uniform_int_distribution<int> dist(1, 1000);

    while (true) {
        const uint16_t n = static_cast<uint16_t>(dist(rng));
        std::vector<uint8_t> msg((1u + n) * sizeof(uint16_t));
        auto* p = reinterpret_cast<uint16_t*>(msg.data());
        p[0] = n;
        for (uint16_t i = 1; i <= n; ++i) {
            p[i] = i;
        }
        // 除了不给自身和守护进程发消息，其他都发
        for (uint8_t i = 0; i < IpcInterface::Define::kShmNameCount; i++) {
            if (i == IpcInterface::Define::Daemon_Fd || i == IpcInterface::Define::Process1_Fd) {
                continue;
            }
            mgr->send(msg, IpcInterface::Define::MESSAGE_ID_PROCESS, IpcInterface::Define::kShmNames[i]);
        }
        // 休眠10ms，小槽数量下，避免单个发送线程过载
        usleep(10000);
    }
    return 0;
}
