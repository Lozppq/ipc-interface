#pragma once

#include <cstdint>

namespace IpcInterface {
namespace Log {

// 打印日志级别
enum{
    Level_Info = 0x01,
    Level_Warning = 0x02,
    Level_Error = 0x04,
    Level_Debug = 0x08,
    Level_All = 0x0F,
};

// 打印日志级别控制的一个变量，控制哪些级别的日志可以打印，直接定义一个无符号整数
extern uint32_t g_log_level;

// 设置日志级别
void setLogLevel(uint32_t level);

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
     * @param fmt 格式化字符串，后接可变参数
    */
    void printLog(int level, const char* fmt, ...);
};

// 日志打印宏
#define LOG_INFO(fmt, ...) LogPrint::getInstance()->printLog(Level_Info, fmt, ##__VA_ARGS__)
#define LOG_WARN(fmt, ...) LogPrint::getInstance()->printLog(Level_Warning, fmt, ##__VA_ARGS__)
#define LOG_ERROR(fmt, ...) LogPrint::getInstance()->printLog(Level_Error, fmt, ##__VA_ARGS__)
#define LOG_DEBUG(fmt, ...) LogPrint::getInstance()->printLog(Level_Debug, fmt, ##__VA_ARGS__)


} // namespace Log
} // namespace IpcInterface
