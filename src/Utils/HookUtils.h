#ifndef BML_HOOKUTILS_H
#define BML_HOOKUTILS_H

#include <cstddef>
#include <cassert>
#include <cstring>
#include <type_traits>

namespace utils {
    void OutputDebugA(const char *format, ...);
    void OutputDebugW(const wchar_t *format, ...);

    template<typename T>
    void *TypeErase(T target) {
        return *reinterpret_cast<void **>(&target);
    }

    template<typename T>
    T ForceReinterpretCast(void *addr) {
        // A native member pointer also contains a this-adjustment. A vtable
        // entry is only a code address: never read that adjustment past addr.
        static_assert(sizeof(T) >= sizeof(addr), "Function address does not fit");
        static_assert(std::is_trivially_copyable<T>::value, "Expected pointer representation");
        T result{};
        std::memcpy(&result, &addr, sizeof(addr));
        return result;
    }

    template<typename T>
    T ForceReinterpretCast(void *base, size_t offset) {
        void *p = static_cast<char *>(base) + offset;
        return ForceReinterpretCast<T>(p);
    }

#ifndef _WIN32
    // Obtain a slot from the compiler's virtual-member encoding, not the byte
    // offset in a struct of member pointers. Native member pointers have two
    // words and the ABI has two destructor slots; neither matches the Win32
    // layout used by VTables.h. Only unadjusted single-inheritance methods are
    // used by these hooks.
    template<typename T>
    std::size_t VirtualMethodSlot(T method) {
        static_assert(std::is_member_function_pointer<T>::value, "Expected a virtual method");
        struct Encoding { std::ptrdiff_t pointer; std::ptrdiff_t adjustment; } value{};
        static_assert(sizeof(method) == sizeof(value), "Unsupported member-pointer ABI");
        std::memcpy(&value, &method, sizeof(value));
#if defined(__arm__) || defined(__aarch64__) || defined(__arm64__) || defined(__wasm__)
        assert(value.adjustment == 1); // ARM and WASM put the virtual bit in adjustment.
        const auto offset = value.pointer;
#else
        assert((value.pointer & 1) && value.adjustment == 0);
        const auto offset = value.pointer - 1;
#endif
        assert(offset >= 0 && offset % sizeof(void *) == 0);
        return static_cast<std::size_t>(offset) / sizeof(void *);
    }
#endif

    inline void **GetVTable(void *instance) {
        if (instance) {
            return *static_cast<void ***>(instance);
        } else {
            return nullptr;
        }
    }

}

#endif // BML_HOOKUTILS_H
