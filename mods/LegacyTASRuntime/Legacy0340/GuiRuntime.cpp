#include "GuiRuntime.h"
#include "Loader/ModManager.h"
#include <algorithm>
#include <vector>

namespace LegacyBGui {
void Detail::World::Invalidate() noexcept {
    for (auto &entry:slots) if (auto slot=entry.second.lock()) slot->valid=false;
    slots.clear();context=nullptr;
}
void Detail::World::Deleting(CK_ID *ids,int count) noexcept {
    for (int i=0;i<count;++i) {
        auto entry=slots.find(ids[i]);
        if (entry!=slots.end()) {
            if (auto slot=entry->second.lock()) slot->valid=false;
            slots.erase(entry);
        }
    }
}
Runtime::Runtime(IBML *bml,LegacyExecuteBB::Runtime &helpers):m_bml(bml),m_helpers(&helpers) {
    if (!bml || !bml->GetCKContext()) throw std::invalid_argument("legacy GUI needs a live host");
    helpers.Validate();
    auto *ctx=bml->GetCKContext();
    m_world=std::make_shared<Detail::World>(ctx);
    auto *manager=ModManager::GetManager(ctx);
    if (!manager) throw std::runtime_error("legacy GUI requires the BML manager");
    manager->RegisterLegacyGuiWorld(m_world);
}
IBML *Runtime::BML() const {
    if (!m_world->context) throw std::logic_error("legacy GUI world ended");
    return m_bml;
}
LegacyExecuteBB::Runtime &Runtime::Helpers() const {
    BML();m_helpers->Validate();return *m_helpers;
}
}
