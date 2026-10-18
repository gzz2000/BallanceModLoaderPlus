#include "HookUtils.h"

#include <cstdarg>
#include <cstdio>
#include <cwchar>

namespace utils {
void OutputDebugA(const char *format, ...) {
    va_list args;
    va_start(args, format);
    std::vfprintf(stderr, format, args);
    va_end(args);
}

void OutputDebugW(const wchar_t *format, ...) {
    va_list args;
    va_start(args, format);
    std::vfwprintf(stderr, format, args);
    va_end(args);
}
}
