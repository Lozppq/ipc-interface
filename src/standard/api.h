/**
 * @file api.h
 * @brief 进程间通信标准API，用于消息处理等接口
 */

#pragma once

#include <cstdint>

namespace IpcInterface {
namespace Standard {

// 将一个u16按照小端模式转换为两个u8
void Small_U16ToU8(uint16_t value, uint8_t* data);
// 将一个u16按照大端模式转换为两个u8
void Big_U16ToU8(uint16_t value, uint8_t* data);

// 将两个u8按照小端模式转换为一个u16
uint16_t Small_U8ToU16(const uint8_t* data);
// 将两个u8按照大端模式转换为一个u16
uint16_t Big_U8ToU16(const uint8_t* data);

// 将一个u32按照小端模式转换为四个u8
void Small_U32ToU8(uint32_t value, uint8_t* data);
// 将一个u32按照大端模式转换为四个u8
void Big_U32ToU8(uint32_t value, uint8_t* data);

// 将四个u8按照小端模式转换为一个u32
uint32_t Small_U8ToU32(const uint8_t* data);
// 将四个u8按照大端模式转换为一个u32
uint32_t Big_U8ToU32(const uint8_t* data);

// 将一个u64按照小端模式转换为八个u8
void Small_U64ToU8(uint64_t value, uint8_t* data);
// 将一个u64按照大端模式转换为八个u8
void Big_U64ToU8(uint64_t value, uint8_t* data);

// 将八个u8按照小端模式转换为一个u64
uint64_t Small_U8ToU64(const uint8_t* data);
// 将八个u8按照大端模式转换为一个u64
uint64_t Big_U8ToU64(const uint8_t* data);

} // namespace Standard
} // namespace IpcInterface
