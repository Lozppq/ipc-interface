/**
 * @file EventHandle.h
 * @brief eventfd 封装：创建、关闭、唤醒、poll 等待
 */

#pragma once

#include <cstdint>

namespace IpcInterface {
namespace Model {

class EventHandle {
public:
    EventHandle();
    ~EventHandle();

    EventHandle(const EventHandle&) = delete;
    EventHandle& operator=(const EventHandle&) = delete;

    // 增加可以使用std::move的构造函数
    EventHandle(EventHandle&& other) noexcept;
    EventHandle& operator=(EventHandle&& other) noexcept;

    void wake();
    uint64_t wait(int timeout_ms);
    void close();

private:
    int m_fd{-1};
};

} // namespace Model
} // namespace IpcInterface
