/**
 * @file udp_process.cpp
 * @brief 本机 UDP 对照：载荷与 process_1/2/3 相同，三进程互相发送并统计 recv rate
 *
 * 用法（三个终端，或后台）：
 *   ./udp_process 1
 *   ./udp_process 2
 *   ./udp_process 3
 */
#include "log/Log_Print.h"
#include <arpa/inet.h>
#include <cerrno>
#include <netinet/in.h>
#include <sys/socket.h>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>
#include <string>
#include <thread>
#include <unistd.h>
#include <vector>

namespace {

constexpr int kProcCount = 3;
constexpr uint16_t kBasePort = 51001;
constexpr int kSockBuf = 4 * 1024 * 1024;

sockaddr_in make_addr(uint16_t port) {
    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_port = htons(port);
    inet_pton(AF_INET, "127.0.0.1", &a.sin_addr);
    return a;
}

bool check_payload(const uint8_t* data, ssize_t nbyte) {
    if (nbyte < static_cast<ssize_t>(sizeof(uint16_t)) || (nbyte % sizeof(uint16_t)) != 0) {
        return false;
    }
    const auto* p = reinterpret_cast<const uint16_t*>(data);
    const uint16_t n = p[0];
    const size_t expect = (1u + n) * sizeof(uint16_t);
    if (n < 1 || n > 1000 || static_cast<size_t>(nbyte) != expect) {
        return false;
    }
    for (uint16_t i = 1; i <= n; ++i) {
        if (p[i] != i) return false;
    }
    return true;
}

} // namespace

int main(int argc, char* argv[]) {
    if (argc != 2) {
        std::fprintf(stderr, "usage: %s <1|2|3>\n", argv[0]);
        return 1;
    }
    const int id = std::atoi(argv[1]);
    if (id < 1 || id > kProcCount) {
        std::fprintf(stderr, "id must be 1, 2 or 3\n");
        return 1;
    }

    const std::string prefix = "udp_" + std::to_string(id);
    IpcInterface::Log::setLogPrefix(prefix.c_str());

    const int fd = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) {
        LOG_ERROR("socket failed: %s", std::strerror(errno));
        return 1;
    }
    ::setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &kSockBuf, sizeof(kSockBuf));
    ::setsockopt(fd, SOL_SOCKET, SO_SNDBUF, &kSockBuf, sizeof(kSockBuf));

    const uint16_t my_port = static_cast<uint16_t>(kBasePort + id - 1);
    const sockaddr_in bind_addr = make_addr(my_port);
    if (::bind(fd, reinterpret_cast<const sockaddr*>(&bind_addr), sizeof(bind_addr)) < 0) {
        LOG_ERROR("bind %u failed: %s", my_port, std::strerror(errno));
        ::close(fd);
        return 1;
    }

    sockaddr_in peers[kProcCount];
    for (int i = 0; i < kProcCount; ++i) {
        peers[i] = make_addr(static_cast<uint16_t>(kBasePort + i));
    }

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

    std::thread([fd, &recv_bytes, &recv_pkts]() {
        uint8_t buf[4096];
        while (true) {
            const ssize_t n = ::recvfrom(fd, buf, sizeof(buf), 0, nullptr, nullptr);
            if (n <= 0) continue;
            recv_bytes.fetch_add(static_cast<uint64_t>(n), std::memory_order_relaxed);
            recv_pkts.fetch_add(1, std::memory_order_relaxed);
            if (!check_payload(buf, n)) {
                LOG_ERROR("recv bad size=%zd", n);
            }
        }
    }).detach();

    LOG_INFO("udp_%d started, pid=%d port=%u", id, getpid(), my_port);
    sleep(1);

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
        for (int i = 0; i < kProcCount; ++i) {
            if (i + 1 == id) continue;
            ::sendto(fd, msg.data(), msg.size(), 0,
                     reinterpret_cast<const sockaddr*>(&peers[i]), sizeof(peers[i]));
            send_bytes.fetch_add(static_cast<uint64_t>(msg.size()), std::memory_order_relaxed);
            send_pkts.fetch_add(1, std::memory_order_relaxed);
        }
    }
}
