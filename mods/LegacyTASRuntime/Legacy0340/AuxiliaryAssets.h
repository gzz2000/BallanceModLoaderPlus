#pragma once
#include "ExecuteBB.h"
#include <array>

namespace Legacy0340 {
// BML 0.3.40's reusable travel-mode templates. The owning CKContext owns the
// loaded objects; this holder must not outlive it or be loaded twice.
struct AuxiliaryAssets {
    std::array<CK3dEntity *,4> physicalTemplates{};
    CKCamera *travelCamera=nullptr;
    void Load(CKContext *context);
};
}
