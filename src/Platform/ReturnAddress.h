#pragma once

#if defined(_MSC_VER)
#include <intrin.h>
#define BML_RETURN_ADDRESS() _ReturnAddress()
#else
#define BML_RETURN_ADDRESS() __builtin_return_address(0)
#endif
