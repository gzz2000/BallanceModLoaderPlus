#include "Hooks/VTablePatch.h"

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <limits>
#include <sys/mman.h>
#include <unistd.h>

namespace {
VTablePatchResult Result(VTablePatchError code,
                         std::size_t index = static_cast<std::size_t>(-1)) {
    return {code, index};
}

bool MakeWritable(void *address, std::size_t size) {
    const long pageSize = ::sysconf(_SC_PAGESIZE);
    if (!address || !size || pageSize <= 0)
        return false;
    const std::uintptr_t mask = static_cast<std::uintptr_t>(pageSize - 1);
    const std::uintptr_t begin = reinterpret_cast<std::uintptr_t>(address) & ~mask;
    const std::uintptr_t end = (reinterpret_cast<std::uintptr_t>(address) + size + mask) & ~mask;
    return ::mprotect(reinterpret_cast<void *>(begin), end - begin,
                      PROT_READ | PROT_WRITE) == 0;
}

bool MakeReadOnly(void *address, std::size_t size) {
    const long pageSize = ::sysconf(_SC_PAGESIZE);
    if (!address || !size || pageSize <= 0)
        return false;
    const std::uintptr_t mask = static_cast<std::uintptr_t>(pageSize - 1);
    const std::uintptr_t begin = reinterpret_cast<std::uintptr_t>(address) & ~mask;
    const std::uintptr_t end = (reinterpret_cast<std::uintptr_t>(address) + size + mask) & ~mask;
    return ::mprotect(reinterpret_cast<void *>(begin), end - begin, PROT_READ) == 0;
}
}

VTablePatch::~VTablePatch() { assert(!IsInstalled()); }

VTablePatchResult VTablePatch::Install(void *instance, const Request *requests,
                                      std::size_t count) {
    if (IsInstalled()) return Result(VTablePatchError::AlreadyInstalled);
    if (!instance) return Result(VTablePatchError::InvalidInstance);
    if (!requests || !count) return Result(VTablePatchError::InvalidRequest);
    void **vtable = *static_cast<void ***>(instance);
    if (!vtable) return Result(VTablePatchError::InvalidVTable);

    std::size_t minimum = (std::numeric_limits<std::size_t>::max)();
    std::size_t maximum = 0;
    std::vector<Entry> entries;
    entries.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        if (!requests[i].Replacement)
            return Result(VTablePatchError::InvalidReplacement, i);
        for (std::size_t j = 0; j < i; ++j)
            if (requests[j].Slot == requests[i].Slot)
                return Result(VTablePatchError::DuplicateSlot, i);
        void *original = vtable[requests[i].Slot];
        if (!original) return Result(VTablePatchError::InvalidOriginal, i);
        entries.push_back({requests[i].Slot, original, requests[i].Replacement});
        minimum = std::min(minimum, requests[i].Slot);
        maximum = std::max(maximum, requests[i].Slot);
    }

    void *region = vtable + minimum;
    const std::size_t regionSize = (maximum - minimum + 1) * sizeof(void *);
    if (!MakeWritable(region, regionSize))
        return Result(VTablePatchError::ProtectionChangeFailed);
    for (const Entry &entry : entries)
        vtable[entry.Slot] = entry.Replacement;
    if (!MakeReadOnly(region, regionSize)) {
        for (const Entry &entry : entries)
            vtable[entry.Slot] = entry.Original;
        MakeReadOnly(region, regionSize);
        return Result(VTablePatchError::ProtectionRestoreFailed);
    }
    m_VTable = vtable;
    m_Region = region;
    m_RegionSize = regionSize;
    m_Entries = std::move(entries);
    return {};
}

VTablePatchResult VTablePatch::Remove() {
    if (!IsInstalled()) return {};
    if (!MakeWritable(m_Region, m_RegionSize))
        return Result(VTablePatchError::ProtectionChangeFailed);
    std::size_t conflict = static_cast<std::size_t>(-1);
    for (std::size_t i = 0; i < m_Entries.size(); ++i) {
        const Entry &entry = m_Entries[i];
        if (m_VTable[entry.Slot] == entry.Replacement)
            m_VTable[entry.Slot] = entry.Original;
        else if (conflict == static_cast<std::size_t>(-1))
            conflict = i;
    }
    if (!MakeReadOnly(m_Region, m_RegionSize))
        return Result(VTablePatchError::ProtectionRestoreFailed);
    Clear();
    return conflict == static_cast<std::size_t>(-1)
        ? VTablePatchResult{}
        : Result(VTablePatchError::OwnershipLost, conflict);
}

void *VTablePatch::GetOriginal(std::size_t slot) const {
    for (const Entry &entry : m_Entries)
        if (entry.Slot == slot) return entry.Original;
    return nullptr;
}

const char *VTablePatch::GetErrorName(VTablePatchError error) {
    switch (error) {
    case VTablePatchError::None: return "none";
    case VTablePatchError::AlreadyInstalled: return "already installed";
    case VTablePatchError::InvalidRequest: return "invalid request";
    case VTablePatchError::InvalidInstance: return "invalid instance";
    case VTablePatchError::InvalidVTable: return "invalid vtable";
    case VTablePatchError::InvalidSlot: return "invalid slot";
    case VTablePatchError::DuplicateSlot: return "duplicate slot";
    case VTablePatchError::InvalidOriginal: return "invalid original function";
    case VTablePatchError::InvalidReplacement: return "invalid replacement function";
    case VTablePatchError::NonExecutableOriginal: return "original function is not executable";
    case VTablePatchError::NonExecutableReplacement: return "replacement function is not executable";
    case VTablePatchError::UnsupportedProtectionLayout: return "unsupported protection layout";
    case VTablePatchError::ProtectionChangeFailed: return "failed to change memory protection";
    case VTablePatchError::SlotChanged: return "vtable slot changed during installation";
    case VTablePatchError::ProtectionRestoreFailed: return "failed to restore memory protection";
    case VTablePatchError::OwnershipLost: return "vtable slot ownership changed";
    }
    return "unknown";
}

void VTablePatch::Clear() {
    m_VTable = nullptr;
    m_Region = nullptr;
    m_RegionSize = 0;
    m_Entries.clear();
}
