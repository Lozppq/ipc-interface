/**
 * @file api.cpp
 * @brief 进程间通信标准API，用于消息处理等接口
 */

#include "api.h"

namespace IpcInterface {
namespace Standard {

void Small_U16ToU8(uint16_t value, uint8_t* data) {
    data[0] = static_cast<uint8_t>(value & 0xFF);
    data[1] = static_cast<uint8_t>((value >> 8) & 0xFF);
}

void Big_U16ToU8(uint16_t value, uint8_t* data) {
    data[0] = static_cast<uint8_t>((value >> 8) & 0xFF);
    data[1] = static_cast<uint8_t>(value & 0xFF);
}

uint16_t Small_U8ToU16(const uint8_t* data) {
    return static_cast<uint16_t>(data[0])
        | (static_cast<uint16_t>(data[1]) << 8);
}

uint16_t Big_U8ToU16(const uint8_t* data) {
    return (static_cast<uint16_t>(data[0]) << 8)
        | static_cast<uint16_t>(data[1]);
}

void Small_U32ToU8(uint32_t value, uint8_t* data) {
    data[0] = static_cast<uint8_t>(value & 0xFF);
    data[1] = static_cast<uint8_t>((value >> 8) & 0xFF);
    data[2] = static_cast<uint8_t>((value >> 16) & 0xFF);
    data[3] = static_cast<uint8_t>((value >> 24) & 0xFF);
}

void Big_U32ToU8(uint32_t value, uint8_t* data) {
    data[0] = static_cast<uint8_t>((value >> 24) & 0xFF);
    data[1] = static_cast<uint8_t>((value >> 16) & 0xFF);
    data[2] = static_cast<uint8_t>((value >> 8) & 0xFF);
    data[3] = static_cast<uint8_t>(value & 0xFF);
}

uint32_t Small_U8ToU32(const uint8_t* data) {
    return static_cast<uint32_t>(data[0])
        | (static_cast<uint32_t>(data[1]) << 8)
        | (static_cast<uint32_t>(data[2]) << 16)
        | (static_cast<uint32_t>(data[3]) << 24);
}

uint32_t Big_U8ToU32(const uint8_t* data) {
    return (static_cast<uint32_t>(data[0]) << 24)
        | (static_cast<uint32_t>(data[1]) << 16)
        | (static_cast<uint32_t>(data[2]) << 8)
        | static_cast<uint32_t>(data[3]);
}

void Small_U64ToU8(uint64_t value, uint8_t* data) {
    data[0] = static_cast<uint8_t>(value & 0xFF);
    data[1] = static_cast<uint8_t>((value >> 8) & 0xFF);
    data[2] = static_cast<uint8_t>((value >> 16) & 0xFF);
    data[3] = static_cast<uint8_t>((value >> 24) & 0xFF);
    data[4] = static_cast<uint8_t>((value >> 32) & 0xFF);
    data[5] = static_cast<uint8_t>((value >> 40) & 0xFF);
    data[6] = static_cast<uint8_t>((value >> 48) & 0xFF);
    data[7] = static_cast<uint8_t>((value >> 56) & 0xFF);
}

void Big_U64ToU8(uint64_t value, uint8_t* data) {
    data[0] = static_cast<uint8_t>((value >> 56) & 0xFF);
    data[1] = static_cast<uint8_t>((value >> 48) & 0xFF);
    data[2] = static_cast<uint8_t>((value >> 40) & 0xFF);
    data[3] = static_cast<uint8_t>((value >> 32) & 0xFF);
    data[4] = static_cast<uint8_t>((value >> 24) & 0xFF);
    data[5] = static_cast<uint8_t>((value >> 16) & 0xFF);
    data[6] = static_cast<uint8_t>((value >> 8) & 0xFF);
    data[7] = static_cast<uint8_t>(value & 0xFF);
}

uint64_t Small_U8ToU64(const uint8_t* data) {
    return static_cast<uint64_t>(data[0])
        | (static_cast<uint64_t>(data[1]) << 8)
        | (static_cast<uint64_t>(data[2]) << 16)
        | (static_cast<uint64_t>(data[3]) << 24)
        | (static_cast<uint64_t>(data[4]) << 32)
        | (static_cast<uint64_t>(data[5]) << 40)
        | (static_cast<uint64_t>(data[6]) << 48)
        | (static_cast<uint64_t>(data[7]) << 56);
}

uint64_t Big_U8ToU64(const uint8_t* data) {
    return (static_cast<uint64_t>(data[0]) << 56)
        | (static_cast<uint64_t>(data[1]) << 48)
        | (static_cast<uint64_t>(data[2]) << 40)
        | (static_cast<uint64_t>(data[3]) << 32)
        | (static_cast<uint64_t>(data[4]) << 24)
        | (static_cast<uint64_t>(data[5]) << 16)
        | (static_cast<uint64_t>(data[6]) << 8)
        | static_cast<uint64_t>(data[7]);
}

} // namespace Standard
} // namespace IpcInterface
