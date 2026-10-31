#include "Hooks/VTablePatch.h"
#include "HookUtils.h"
#include "CKBaseManager.h"
#include "CKInputManager.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {
void Check(bool value, const char *message) {
    if (!value) {
        std::fprintf(stderr, "FAIL: %s\n", message);
        std::abort();
    }
}

struct Manager {
    virtual ~Manager() = default;
    virtual int Before() { return 11; }
    virtual int Process(int amount) { return value += amount; }
    virtual int Initialize() { value = 0; return 22; }
    int value = 40;
};
using Method = int (Manager::*)(int);
Method original{};
int calls = 0;
struct Hook {
    int Process(int amount) {
        ++calls;
        return (reinterpret_cast<Manager *>(this)->*original)(amount) + 100;
    }
};

// Adjacent empty callbacks may merge in WASM, but an override must still be
// detected at its own slot, including the ABI's destructor entries.
struct Callbacks {
    virtual ~Callbacks() = default;
    virtual void Load() {}
    virtual void Render(int) {}
    virtual void Physicalize(void *, float) {}
    virtual void Unphysicalize(void *) {}
};
struct PhysicsCallbacks : Callbacks {
    void Physicalize(void *, float) override { ++calls; }
    void Unphysicalize(void *) override { ++calls; }
};

// Keep the test's dispatch virtual even when the optimizer knows the concrete
// instance at the call site; no LTO is used by the standalone sanitizer run.
__attribute__((noinline)) int Dispatch(Manager *manager, int amount) {
    return manager->Process(amount);
}
}

int main() {
    Callbacks blank;
    PhysicsCallbacks physics;
    auto **base = utils::GetVTable(&blank);
    auto **derived = utils::GetVTable(&physics);
    Check(base[utils::VirtualMethodSlot(&Callbacks::Load)] ==
          derived[utils::VirtualMethodSlot(&Callbacks::Load)], "inherited callback excluded");
    Check(base[utils::VirtualMethodSlot(&Callbacks::Physicalize)] !=
          derived[utils::VirtualMethodSlot(&Callbacks::Physicalize)], "physicalize override discovered");
    Check(base[utils::VirtualMethodSlot(&Callbacks::Unphysicalize)] !=
          derived[utils::VirtualMethodSlot(&Callbacks::Unphysicalize)], "unphysicalize override discovered");
    Check(utils::VirtualMethodSlot(&CKBaseManager::PostProcess) == 7,
          "native PostProcess slot must include both destructor entries");
    Check(utils::VirtualMethodSlot(&CKBaseManager::OnCKInit) == 12,
          "former incorrect slot identifies OnCKInit");
    Check(utils::VirtualMethodSlot(&CKInputManager::PostProcess) == 7,
          "input inherited method uses base slot");
    Check(utils::VirtualMethodSlot(&CKInputManager::GetKeyboardState) == 38,
          "input keyboard slot follows native manager entries");

    Manager manager;
    const auto slot = utils::VirtualMethodSlot(&Manager::Process);
    VTablePatch patch;
    const VTablePatch::Request request{slot, utils::TypeErase(&Hook::Process)};
    Check(static_cast<bool>(patch.Install(&manager, &request, 1)), "install actual vtable patch");
    original = utils::ForceReinterpretCast<Method>(patch.GetOriginal(slot));
    // The representation's second word must be initialized, not read beyond
    // an address-sized temporary. ASan also checks this conversion's accesses.
    unsigned char representation[sizeof(original)];
    std::memcpy(representation, &original, sizeof(original));
    for (std::size_t i = sizeof(void *); i < sizeof(original); ++i)
        Check(representation[i] == 0, "original member pointer has zero adjustment");
    Check(Dispatch(&manager, 2) == 142 && manager.value == 42 && calls == 1,
          "patched dispatch chains the intended original on the same instance");
    Check(manager.Before() == 11 && manager.value == 42, "neighbor method unchanged");
    Check(static_cast<bool>(patch.Remove()), "restore actual vtable");
    Check(Dispatch(&manager, 3) == 45 && calls == 1, "restored method bypasses hook");
    std::puts("PASS native slots, patched dispatch, original member pointer and restoration");
}
