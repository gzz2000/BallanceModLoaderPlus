#pragma once

class IBML;
class IMod;

// Source-integrated hosts register these before creating a CK context. The
// loader owns each resulting instance for the same lifetime as a dynamic mod.
struct BMLStaticModFactory {
    const char *name;
    IMod *(*create)(IBML *);
    void (*destroy)(IMod *);
};
bool BML_RegisterStaticModFactory(const BMLStaticModFactory &factory);
