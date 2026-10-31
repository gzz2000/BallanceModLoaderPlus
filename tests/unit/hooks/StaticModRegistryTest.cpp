#include "Loader/StaticModRegistry.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

static void Check(bool ok, const char *message) {
    if (!ok) { std::fprintf(stderr, "FAIL: %s\n", message); std::abort(); }
}
static IMod *Create(IBML *) { return nullptr; }
static IMod *OtherCreate(IBML *) { return nullptr; }
static void Destroy(IMod *) {}
int main() {
    Check(BML_StaticModFactories().empty(), "registry starts empty");
    Check(!BML_RegisterStaticModFactory({nullptr, Create, Destroy}), "null name rejected");
    Check(!BML_RegisterStaticModFactory({"", Create, Destroy}), "empty name rejected");
    Check(!BML_RegisterStaticModFactory({"bad", nullptr, Destroy}), "missing factory rejected");
    Check(!BML_RegisterStaticModFactory({"bad", Create, nullptr}), "missing destructor rejected");
    {
        std::string temporary = "First";
        Check(BML_RegisterStaticModFactory({temporary.c_str(), Create, Destroy}), "registration");
        temporary.assign(100, 'x');
    }
    Check(BML_RegisterStaticModFactory({"First", Create, Destroy}), "same registration is idempotent");
    Check(!BML_RegisterStaticModFactory({"First", OtherCreate, Destroy}), "conflicting owner rejected");
    auto snapshot = BML_StaticModFactories();
    Check(snapshot.size() == 1 && std::strcmp(snapshot[0].name, "First") == 0,
          "registry owns the name after caller storage expires");
    for (int i = 0; i != 1024; ++i) {
        const auto name = "extra-" + std::to_string(i);
        Check(BML_RegisterStaticModFactory({name.c_str(), Create, Destroy}), "grow registry");
    }
    Check(std::strcmp(snapshot[0].name, "First") == 0 && snapshot[0].destroy == Destroy,
          "snapshot and destructor stay valid after registry growth");
    Check(BML_StaticModFactories().size() == 1025, "duplicates did not add instances");
    std::puts("PASS static factory validation, identity, name lifetime and snapshots");
}
