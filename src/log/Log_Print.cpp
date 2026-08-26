/**
 * @file Log_Print.cpp
 * @brief 日志打印
*/

#include "Log_Print.h"
#include <cstdio>
#include <cstdarg>
#include <ctime>
#include <string>
#if defined(__linux__)
#include <unistd.h>
#include <sys/syscall.h>
#endif

namespace IpcInterface {
namespace Log {

// 仅本翻译单元使用；static 保证内部链接，不进头文件
static std::string g_log_prefix_storage = "unknown";

// 默认不打印debug级别的日志
static uint32_t g_log_level = Level_Info;

void setLogLevel(uint32_t level) {
    g_log_level = level;
}

void setLogPrefix(const char* prefix) {
    if (prefix && prefix[0] != '\0') {
        g_log_prefix_storage = prefix;
    } else {
        g_log_prefix_storage = "unknown";
    }
}

LogPrint::LogPrint() {
}

LogPrint::~LogPrint() {
}

LogPrint* LogPrint::getInstance() {
    static LogPrint instance;
    return &instance;
}

void LogPrint::printLog(uint32_t level, const char* func, int line, const char* fmt, ...) {
    if (!fmt || level > g_log_level) 
        return;

    const char* level_tag = "UNKNOWN";
    switch (level) {
        case Level_Info: level_tag = "INFO"; break;
        case Level_Warning: level_tag = "WARN"; break;
        case Level_Error: level_tag = "ERROR"; break;
        case Level_Debug: level_tag = "DEBUG"; break;
        default: break;
    }

    char time_buf[32];
    struct tm tm_now {};
    int ms = 0;
#if defined(__linux__)
    struct timespec ts {};
    clock_gettime(CLOCK_REALTIME, &ts);
    localtime_r(&ts.tv_sec, &tm_now);
    ms = static_cast<int>(ts.tv_nsec / 1000000);
#else
    time_t now = time(nullptr);
    if (struct tm* p = localtime(&now)) tm_now = *p;
#endif
    size_t len = strftime(time_buf, sizeof(time_buf), "%Y-%m-%d %H:%M:%S", &tm_now);
    if (len > 0 && len + 4 < sizeof(time_buf)) {
        snprintf(time_buf + len, sizeof(time_buf) - len, ".%03d", ms);
    }

#if defined(__linux__)
    long tid = syscall(SYS_gettid);
#else
    long tid = 0;
#endif

    char buf[1024];
    // 预留 " -- func:line\n\0"
    constexpr int kTail = 160;
    constexpr int kBodyCap = static_cast<int>(sizeof(buf)) - kTail;
    int off = snprintf(buf, kBodyCap + 1, "%s %s [%s] [%ld] ",
                       time_buf, level_tag, g_log_prefix_storage.c_str(), tid);
    if (off < 0) off = 0;
    else if (off > kBodyCap) off = kBodyCap;
    va_list args;
    va_start(args, fmt);
    int n = vsnprintf(buf + off, static_cast<size_t>(kBodyCap - off + 1), fmt, args);
    va_end(args);
    if (n > 0) {
        off += n;
        if (off > kBodyCap) off = kBodyCap;
    }
    snprintf(buf + off, sizeof(buf) - static_cast<size_t>(off), " -- %s:%d\n",
             func ? func : "?", line);
    std::printf("%s", buf);
}

} // namespace Log
} // namespace IpcInterface
