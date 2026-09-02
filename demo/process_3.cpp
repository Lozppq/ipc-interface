/**
 * @file process_3.cpp
 * @brief process_3 demo：初始化后周期性向所有进程发消息
 */
#include "mul_process/ShmManager.h"
#include "define/Common.h"
#include "define/MessageId.h"
#include "log/Log_Print.h"
#include <atomic>
#include <cstdint>
#include <random>
#include <thread>
#include <unistd.h>

const uint16_t MAX_N = 1000;

int main() {
    IpcInterface::Log::setLogPrefix("process_3");
    auto* mgr = IpcInterface::MulProcess::ShmManager::getInstance();
    mgr->initParams(IpcInterface::Define::Process3);
    mgr->start();

    std::atomic<uint64_t> recv_bytes{0};
    std::atomic<uint64_t> recv_pkts{0};
    std::atomic<uint64_t> send_bytes{0};
    std::atomic<uint64_t> send_pkts{0};
    std::thread([&]() {
        while (true) {
            sleep(1);
            const uint64_t rbytes = recv_bytes.exchange(0, std::memory_order_relaxed);
            const uint64_t rpkts = recv_pkts.exchange(0, std::memory_order_relaxed);
            const uint64_t sbytes = send_bytes.exchange(0, std::memory_order_relaxed);
            const uint64_t spkts = send_pkts.exchange(0, std::memory_order_relaxed);
            LOG_INFO("recv rate=%.3f MB/s pkt=%llu send=%.3f MB/s pkt=%llu",
                     rbytes / (1024.0 * 1024.0), static_cast<unsigned long long>(rpkts),
                     sbytes / (1024.0 * 1024.0), static_cast<unsigned long long>(spkts));
        }
    }).detach();

    mgr->setReceiveHandler([&recv_bytes, &recv_pkts](std::shared_ptr<IpcInterface::MulProcess::TagReceiveMessage> tag) {
        if (!tag) return;
        const auto& data = tag->m_data;
        recv_bytes.fetch_add(data.size(), std::memory_order_relaxed);
        recv_pkts.fetch_add(1, std::memory_order_relaxed);
        if (data.size() < sizeof(uint16_t) || (data.size() % sizeof(uint16_t)) != 0) {
            LOG_ERROR("recv bad size=%zu message_id=%u", data.size(), tag->m_message_id);
            return;
        }
        const auto* p = reinterpret_cast<const uint16_t*>(data.data());
        const uint16_t n = p[0];
        const size_t expect = (1u + n) * sizeof(uint16_t);
        if (n < 1 || n > MAX_N || data.size() != expect) {
            LOG_ERROR("recv bad n=%u size=%zu expect=%zu message_id=%u",
                      n, data.size(), expect, tag->m_message_id);
            return;
        }
    });

    LOG_INFO("process_3 started, pid=%d", getpid());

    std::mt19937 rng{std::random_device{}()};
    std::uniform_int_distribution<int> dist(500, MAX_N);

    sleep(1);
    while (true) {
        auto tag = std::make_shared<IpcInterface::MulProcess::TagSendMessage>();
        const uint16_t n = static_cast<uint16_t>(dist(rng));
        tag->m_data.resize((1u + n) * sizeof(uint16_t));
        auto* p = reinterpret_cast<uint16_t*>(tag->m_data.data());
        p[0] = n;
        for (uint16_t i = 1; i <= n; ++i) {
            p[i] = i;
        }
        tag->m_message_id = IpcInterface::Define::MESSAGE_ID_PROCESS;
        for (uint8_t i = 0; i < IpcInterface::Define::kShmNameCount; i++) {
            if (i == IpcInterface::Define::Daemon_Fd || i == IpcInterface::Define::Process3_Fd) {
                continue;
            }
            if (mgr->send(tag, IpcInterface::Define::kShmNames[i])) {
                send_bytes.fetch_add(tag->m_data.size(), std::memory_order_relaxed);
                send_pkts.fetch_add(1, std::memory_order_relaxed);
            }
        }
    }
    return 0;
}
