#pragma once
#include "OwnedParameters.h"
#include <vector>
#include <cstdio>

namespace PracticeTimeline {
// CK-managed copies are NOT immutable references: CheckPostDeletion clears
// object IDs and collection entries even in private snapshot parameters.
// Keep typed IDs outside CK. Never interpret an arbitrary integer as an ID.
struct ParameterReferences {
    CK_ID id = 0;
    CKGUID guid = CKGUID(0, 0);
    enum Kind { Value, Object, Collection, Structure } kind = Value;
    std::vector<CK_ID> targets;
    std::vector<ParameterReferences> members;
};
inline CKERROR CaptureParameterReferences(CKParameter *p, ParameterReferences &out,
                                          std::unordered_set<CK_ID> &active) {
    if (!p || !active.insert(p->GetID()).second) return CKERR_INVALIDOBJECT;
    auto *ctx = p->GetCKContext();
    auto *pm = ctx->GetParameterManager();
    out = ParameterReferences(); out.id = p->GetID(); out.guid = p->GetGUID();
    CKERROR result = CK_OK;
    if (auto *structure = pm->GetStructDescByType(p->GetType())) {
        out.kind = ParameterReferences::Structure;
        auto *ids = static_cast<const CK_ID *>(p->GetReadDataPtr(FALSE));
        if (!ids || p->GetDataSize() < structure->NbData * int(sizeof(CK_ID))) result = CKERR_INVALIDOBJECT;
        else for (int i = 0; i < structure->NbData && result == CK_OK; ++i) {
            out.members.emplace_back();
            result = CaptureParameterReferences(CKParameter::Cast(ctx->GetObject(ids[i])), out.members.back(), active);
            if (result != CK_OK) fprintf(stderr, "PRACTICE_REFERENCE_FAIL parent=%u type=%d member=%d child=%u class=%d error=%d\n", unsigned(p->GetID()), p->GetType(), i, unsigned(ids[i]), ctx->GetObject(ids[i]) ? int(ctx->GetObject(ids[i])->GetClassID()) : -1, int(result));
        }
    } else if (p->GetGUID() == CKPGUID_OBJECTARRAY) {
        out.kind = ParameterReferences::Collection;
        XObjectArray *array = nullptr; p->GetValue(&array, FALSE);
        if (!array) result = CKERR_INVALIDOBJECT;
        else for (int i = 0; i < array->Size(); ++i) out.targets.push_back((*array)[i]);
    } else if (auto *desc = pm->GetParameterTypeDescription(p->GetType());
               desc && CKIsChildClassOf(desc->Cid, CKCID_OBJECT)) {
        out.kind = ParameterReferences::Object;
        CK_ID id = 0; result = p->GetValue(&id, FALSE); out.targets.push_back(id);
    }
    active.erase(out.id);
    return result;
}
inline CKERROR CaptureParameterReferences(CKParameter *p, ParameterReferences &out) {
    std::unordered_set<CK_ID> active;
    return CaptureParameterReferences(p, out, active);
}
inline void CollectReferenceIdentities(const ParameterReferences &state,
                                       std::unordered_set<CK_ID> &ids) {
    if (state.id) ids.insert(state.id);
    for (const auto &child : state.members) CollectReferenceIdentities(child, ids);
}
inline CKERROR BindParameterReferenceTree(CKContext *ctx, const ParameterReferences &state,
                                          CKParameter *p, RebuiltParameters &bindings) {
    if (!p || p->GetGUID() != state.guid) return CKERR_INVALIDOBJECT;
    CKERROR result = BindRebuiltParameter(ctx, bindings, state.id, p);
    if (result != CK_OK || state.kind != ParameterReferences::Structure) return result;
    auto *desc = ctx->GetParameterManager()->GetStructDescByType(p->GetType());
    auto *ids = static_cast<const CK_ID *>(p->GetReadDataPtr(FALSE));
    if (!desc || !ids || desc->NbData != int(state.members.size()) ||
        p->GetDataSize() < int(state.members.size() * sizeof(CK_ID))) return CKERR_INVALIDOBJECT;
    for (size_t i = 0; i < state.members.size(); ++i) {
        result = BindParameterReferenceTree(ctx, state.members[i],
            CKParameter::Cast(ctx->GetObject(ids[i])), bindings);
        if (result != CK_OK) return result;
    }
    return CK_OK;
}
// Call after ALL CopyValue operations with output propagation suspended.
// Copying a containing structure later would otherwise overwrite its children.
template <typename Resolver>
CKERROR RestoreParameterReferences(CKParameter *p, const ParameterReferences &state, Resolver resolve) {
    if (!p || p->GetGUID() != state.guid) return CKERR_INVALIDOBJECT;
    auto *ctx = p->GetCKContext();
    if (state.kind == ParameterReferences::Structure) {
        auto *desc = ctx->GetParameterManager()->GetStructDescByType(p->GetType());
        auto *ids = static_cast<const CK_ID *>(p->GetReadDataPtr(FALSE));
        if (!desc || !ids || desc->NbData != int(state.members.size()) ||
            p->GetDataSize() < int(state.members.size() * sizeof(CK_ID))) return CKERR_INVALIDOBJECT;
        for (size_t i = 0; i < state.members.size(); ++i) {
            CKERROR result = RestoreParameterReferences(CKParameter::Cast(ctx->GetObject(ids[i])), state.members[i], resolve);
            if (result != CK_OK) return result;
        }
    } else if (state.kind != ParameterReferences::Value) {
        std::vector<CK_ID> ids; ids.reserve(state.targets.size());
        for (CK_ID saved : state.targets) {
            CKObject *object = saved ? resolve(saved) : nullptr;
            if (saved && (!object || object->IsToBeDeleted() || object->GetCKContext() != ctx)) return CKERR_INVALIDOBJECT;
            if (object && state.kind == ParameterReferences::Object) {
                auto *desc = ctx->GetParameterManager()->GetParameterTypeDescription(p->GetType());
                if (!desc || !CKIsChildClassOf(object, desc->Cid)) return CKERR_INVALIDOBJECT;
            }
            ids.push_back(object ? object->GetID() : 0);
        }
        if (state.kind == ParameterReferences::Object) {
            if (ids.size() != 1) return CKERR_INVALIDOBJECT;
            if (p->GetObjectFlags() & CK_PARAMETERIN_THIS) {
                CK_ID current = 0;
                if (p->GetDataSize() != sizeof(CK_ID) || p->GetValue(&current, FALSE) != CK_OK)
                    return CKERR_INVALIDOBJECT;
                return current == ids[0] ? CK_OK : CKERR_INVALIDOBJECT;
            }
            return p->SetValue(ids.data(), sizeof(CK_ID));
        }
        XObjectArray *array = nullptr; p->GetValue(&array, FALSE);
        if (!array) return CKERR_INVALIDOBJECT;
        array->Clear(); for (CK_ID id : ids) array->PushBack(id);
    }
    return CK_OK;
}
} // namespace PracticeTimeline
