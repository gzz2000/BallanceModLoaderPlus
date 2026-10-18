#include "Logging/Logger.h"

#include <cstdio>
#include <chrono>
#include <ctime>
#include <string>
#include <vector>

#if BML_ENABLE_ANGELSCRIPT
#include "AngelScript/ScriptDevToolsService.h"
#endif

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#endif

#include "Loader/ModContext.h"

Logger *Logger::m_DefaultLogger = nullptr;

Logger *Logger::GetDefault() {
    return m_DefaultLogger;
}

void Logger::SetDefault(Logger *logger) {
    m_DefaultLogger = logger;
}

Logger::Logger(const char *modName) : m_ModName(modName) {}

void Logger::Info(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    Log("INFO", fmt, args);
    va_end(args);
}

void Logger::Warn(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    Log("WARN", fmt, args);
    va_end(args);
}

void Logger::Error(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    Log("ERROR", fmt, args);
    va_end(args);
}

void Logger::Log(const char *level, const char *fmt, va_list args) {
#if defined(_WIN32)
    SYSTEMTIME sys;
    GetLocalTime(&sys);
#else
    const auto now = std::chrono::system_clock::now();
    const auto milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()) % 1000;
    const std::time_t time = std::chrono::system_clock::to_time_t(now);
    std::tm sys{};
    localtime_r(&time, &sys);
#endif

    auto *ctx = BML_GetModContext();
    FILE *logFile = ctx ? ctx->GetLogFile() : nullptr;

    std::string message;
    if (fmt) {
        va_list sizeArgs;
        va_copy(sizeArgs, args);
        const int length = std::vsnprintf(nullptr, 0, fmt, sizeArgs);
        va_end(sizeArgs);
        if (length >= 0) {
            std::vector<char> buffer(static_cast<size_t>(length) + 1);
            va_list formatArgs;
            va_copy(formatArgs, args);
            std::vsnprintf(buffer.data(), buffer.size(), fmt, formatArgs);
            va_end(formatArgs);
            message.assign(buffer.data());
        }
    }

    FILE *outFiles[] = {
#ifdef _DEBUG
        stdout,
#endif
        logFile
    };

    for (FILE *file: outFiles) {
        if (!file) continue;

#if defined(_WIN32)
        fprintf(file, "[%02d/%02d/%d %02d:%02d:%02d.%03d] ", sys.wMonth, sys.wDay,
                sys.wYear, sys.wHour, sys.wMinute, sys.wSecond, sys.wMilliseconds);
#else
        fprintf(file, "[%02d/%02d/%d %02d:%02d:%02d.%03d] ", sys.tm_mon + 1,
                sys.tm_mday, sys.tm_year + 1900, sys.tm_hour, sys.tm_min,
                sys.tm_sec, static_cast<int>(milliseconds.count()));
#endif
        fprintf(file, "[%s/%s]: ", m_ModName, level);
        fputs(message.c_str(), file);
        fputc('\n', file);
        fflush(file);
    }

#if BML_ENABLE_ANGELSCRIPT
    if (ctx) {
        if (auto *devTools = ctx->GetScriptDevTools())
            devTools->PublishLogLine(level, m_ModName, message);
    }
#endif
}
