/**
 * @file Log_Print.cpp
 * @brief 日志打印
*/

#include "Log_Print.h"
#include <cstdio>
#include <cstdarg>
#include <ctime>
#if defined(__linux__)
#include <unistd.h>
#include <sys/syscall.h>
#endif

namespace IpcInterface {
namespace Log {

// 默认不打印debug级别的日志
uint32_t g_log_level = Level_Info | Level_Warning | Level_Error;

void setLogLevel(uint32_t level) {
    g_log_level = level;
}

LogPrint::LogPrint() {
}

LogPrint::~LogPrint() {
}

LogPrint* LogPrint::getInstance() {
    static LogPrint instance;
    return &instance;
}

void LogPrint::printLog(int level, const char* fmt, ...) {
    if (!fmt) return;

    const char* level_tag = "UNKNOWN";
    switch (level) {
        case Level_Info: level_tag = "INFO"; break;
        case Level_Warning: level_tag = "WARN"; break;
        case Level_Error: level_tag = "ERROR"; break;
        case Level_Debug: level_tag = "DEBUG"; break;
        default: break;
    }

    char time_buf[32];
    time_t now = time(nullptr);
    struct tm tm_now {};
#if defined(__linux__)
    localtime_r(&now, &tm_now);
#else
    if (struct tm* p = localtime(&now)) tm_now = *p;
#endif
    strftime(time_buf, sizeof(time_buf), "%Y-%m-%d %H:%M:%S", &tm_now);

#if defined(__linux__)
    long tid = syscall(SYS_gettid);
#else
    long tid = 0;
#endif

    char buf[1024];
    int off = snprintf(buf, sizeof(buf), "%s %s [%ld]  ", time_buf, level_tag, tid);
    if (off < 0) 
        off = 0;
    else if (off >= static_cast<int>(sizeof(buf))) 
        off = static_cast<int>(sizeof(buf)) - 1;

    va_list args;
    va_start(args, fmt);
    int n = vsnprintf(buf + off, sizeof(buf) - off, fmt, args);
    va_end(args);

    if (n > 0) {
        off += n;
        if (off >= static_cast<int>(sizeof(buf))) 
            off = static_cast<int>(sizeof(buf)) - 1;
    }

    if (off + 1 < static_cast<int>(sizeof(buf))) {
        buf[off++] = '\n';
        buf[off] = '\0';
    }

    // 如果日志级别匹配，则打印到控制台
    if (g_log_level & level){
        std::printf("%s", buf);
    }
}

} // namespace Log
} // namespace IpcInterface
