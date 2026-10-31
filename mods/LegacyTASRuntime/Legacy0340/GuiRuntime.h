// World-scoped ownership adapter for BML 0.3.40 GUI (MIT; see LICENSE).
#pragma once
#include "HelperRuntime.h"
#include <BML/IBML.h>
#include <memory>
#include <unordered_map>
#include <stdexcept>

namespace LegacyBGui {
namespace Detail {
struct Slot {
    CK_ID id = 0;
    CKObject *object = nullptr;
    bool valid = true;
};
struct World {
    explicit World(CKContext *ctx):context(ctx) {}
    CKContext *context;
    std::unordered_map<CK_ID,std::weak_ptr<Slot>> slots;
    bool Live(const std::shared_ptr<Slot>& slot) const {
        return context && slot && slot->valid && context->GetObject(slot->id)==slot->object;
    }
    void Invalidate() noexcept;
    void Deleting(CK_ID *ids,int count) noexcept;
};
}

// Each slot is invalidated before CK deletion, independently of future ID or
// address reuse. Empty references convert to null; stale references throw.
template<class T> class ObjectRef {
public:
    ObjectRef() = default;
    T *Get() const {
        if (!slot) return nullptr;
        if (!world->Live(slot)) throw std::logic_error("legacy GUI reference no longer exists");
        return static_cast<T *>(slot->object);
    }
    T *operator->() const {
        auto *object=Get();
        if (!object) throw std::logic_error("legacy GUI reference is empty");
        return object;
    }
    operator T *() const { return Get(); }
    void Destroy() noexcept {
        if (world && world->Live(slot)) world->context->DestroyObject(slot->id);
        slot.reset();world.reset();
    }
private:
    friend class Runtime;
    std::shared_ptr<Detail::World> world;
    std::shared_ptr<Detail::Slot> slot;
};

// Runtime must outlive its Gui/Element wrappers; helpers and IBML must outlive
// an active world. Context callbacks invalidate references before world teardown.
class Runtime {
public:
    Runtime(IBML *bml,LegacyExecuteBB::Runtime &helpers);
    Runtime(const Runtime&)=delete;
    Runtime& operator=(const Runtime&)=delete;
    IBML *BML() const;
    LegacyExecuteBB::Runtime &Helpers() const;
    void InitMaterials();
    template<class T> ObjectRef<T> Keep(T *object) {
        ObjectRef<T> ref;
        if (!object) return ref;
        if (!m_world->context || object->GetCKContext()!=m_world->context)
            throw std::logic_error("legacy GUI object belongs to another world");
        ref.world=m_world;
        auto &weak=m_world->slots[object->GetID()];ref.slot=weak.lock();
        if (!ref.slot) {
            ref.slot=std::make_shared<Detail::Slot>();
            ref.slot->id=object->GetID();ref.slot->object=object;weak=ref.slot;
        }
        if (!m_world->Live(ref.slot)) throw std::logic_error("legacy GUI stale slot");
        return ref;
    }
    ObjectRef<CKMaterial> up,over,inactive,field,caret,highlight;
    ObjectRef<CKGroup> allSound;
    CKSTRING textFont=nullptr;
private:
    std::shared_ptr<Detail::World> m_world;
    IBML *m_bml;
    LegacyExecuteBB::Runtime *m_helpers;
};
}
