/**
 * @file MessageId.h
 * @brief 消息ID定义，进程ipc标准协议
*/

#pragma once

#include <cstdint>

namespace IpcInterface
{
namespace Define
{

// 默认使用小端字节序传递大于8位的数值

// 消息ID，uint16_t类型
enum : uint16_t
{
    MESSAGE_ID_DAEMON, // 专用于业务进程与守护进程之间通信的ID
    MESSAGE_ID_PROCESS, // 专用于业务进程与业务进程之间通信的ID，消息子ID根据自定义业务需求定义
    MESSAGE_ID_INVALID, // 无效消息ID
};

// MESSAGE_ID_DAEMON消息子ID，uint16_t类型
enum : uint16_t
{
    // 向守护进程申请分配共享内存的ID，
    // 数据部分：u8 逻辑进程id，u8 0代表发送者，1代表接收者，u32 单槽位大小，u32 槽位数量，u8 名称长度n，n个字节的名称
    // 接收者回包在末尾追加 u8 读者下标（0..MAX_READER_COUNT-1），（如果是读者发给业务进程多一个u8，则代表读者标志位是第几位）
    MESSAGE_SUB_ID_ALLOCATE_SHM,

    // 向守护进程申请释放共享内存的ID，
    // 数据部分：u8 共享内存名称长度n，n个字节的名称，u8 逻辑进程id，u8 0正常释放，1强制释放
    MESSAGE_SUB_ID_RELEASE_SHM,

    // 设置同步标志的ID，
    // 数据部分：u8 逻辑进程id，u8 同步标志
    MESSAGE_SUB_ID_SET_SYNC_FLAG,

    // 业务进程 inbox 打开后向 daemon 报到，
    // 数据部分：u8 逻辑进程id，u32 真实 pid
    MESSAGE_SUB_ID_PROCESS_ONLINE,
};

} // namespace Define
} // namespace IpcInterface
