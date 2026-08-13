#pragma once

#include <cstdint>

namespace IpcInterface {
namespace Log {

// 打印日志级别
enum{
    Level_Error = 0x01,
    Level_Warning = 0x02,
    Level_Info = 0x04,
    Level_Debug = 0x08,
    Level_All = 0x0F,
};

// 设置日志级别
void setLogLevel(uint32_t level);

/**
 * @brief 设置日志前缀（内部拷贝保存，调用后一直生效）
 * @param prefix 前缀字符串；空或 nullptr 时回退为 "ipc-interface"
 */
void setLogPrefix(const char* prefix);

class LogPrint {
public:
    LogPrint();
    ~LogPrint();

    /**
     * @brief 单例类
     * @return 单例类指针
    */
    static LogPrint* getInstance();

    /**
     * @brief 打印日志，支持像 printf 一样的格式化
     * @param level 日志级别
     * @param func 调用函数名
     * @param line 调用行号
     * @param fmt 格式化字符串，后接可变参数
    */
    void printLog(uint32_t level, const char* func, int line, const char* fmt, ...);
};

// 日志打印宏
#define LOG_INFO(fmt, ...) ::IpcInterface::Log::LogPrint::getInstance()->printLog(::IpcInterface::Log::Level_Info, __func__, __LINE__, fmt, ##__VA_ARGS__)
#define LOG_WARN(fmt, ...) ::IpcInterface::Log::LogPrint::getInstance()->printLog(::IpcInterface::Log::Level_Warning, __func__, __LINE__, fmt, ##__VA_ARGS__)
#define LOG_ERROR(fmt, ...) ::IpcInterface::Log::LogPrint::getInstance()->printLog(::IpcInterface::Log::Level_Error, __func__, __LINE__, fmt, ##__VA_ARGS__)
#define LOG_DEBUG(fmt, ...) ::IpcInterface::Log::LogPrint::getInstance()->printLog(::IpcInterface::Log::Level_Debug, __func__, __LINE__, fmt, ##__VA_ARGS__)


} // namespace Log
} // namespace IpcInterface
