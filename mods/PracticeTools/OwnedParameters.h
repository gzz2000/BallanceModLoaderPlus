#pragma once

#include "CKAll.h"
#include <unordered_map>
#include <unordered_set>

namespace PracticeTimeline {
// Saved IDs identify semantic slots; live objects are allowed to have fresh
// IDs after their owners reconstruct those slots. Ordinary objects still use
// the stricter lifetime-token resolver in PracticeTools.
using RebuiltParameters = std::unordered_map<CK_ID, CKParameter *>;

inline CKERROR BindRebuiltParameter(CKContext *context, RebuiltParameters &bindings,
                                   CK_ID savedId, CKParameter *parameter) {
    if (!context || !savedId || !parameter ||
        context->GetObject(parameter->GetID()) != parameter || parameter->IsToBeDeleted())
        return CKERR_INVALIDOBJECT;
    const auto inserted = bindings.emplace(savedId, parameter);
    return inserted.second || inserted.first->second == parameter
        ? CK_OK : CKERR_INVALIDOBJECT;
}

// All arrays must have their saved shape before this pass. Then all owned
// parameters (including attributes) must be bound before restoring shortcuts
// or behavior inputs. A surviving shifted cell must be rebound too: its old
// pointer may now occupy a different saved row.
template <typename State>
CKERROR BindArrayParameters(CKContext *context, CKDataArray *array,
                           const State &state, RebuiltParameters &bindings) {
    if (!context || !array) return CKERR_INVALIDPARAMETER;
    const auto &objects = context->GetObjectListByType(CKCID_OBJECT, TRUE);
    std::unordered_set<CKObject *> live(objects.Begin(), objects.End());
    for (const auto &cell : state.cells) {
        if (cell.type != CKARRAYTYPE_PARAMETER || !cell.parameter || !cell.owned) continue;
        auto *element = array->GetElement(cell.row, cell.column);
        auto *parameter = element ? reinterpret_cast<CKParameter *>(*element) : nullptr;
        if (!parameter || !live.count(parameter) || parameter->IsToBeDeleted() ||
            !CKIsChildClassOf(parameter, CKCID_PARAMETER) ||
            parameter->GetGUID() != cell.parameter->GetGUID()) return CKERR_INVALIDOBJECT;
        if (parameter->GetOwner() != array) {
            // The abandoned timeline replaced an owned cell with a shortcut.
            // RemoveShortcut copies the value into a new array-owned object.
            array->RemoveShortcut(cell.row, cell.column);
            element = array->GetElement(cell.row, cell.column);
            parameter = element ? reinterpret_cast<CKParameter *>(*element) : nullptr;
            if (!parameter || parameter->GetOwner() != array) return CKERR_INVALIDOBJECT;
        }
        const CKERROR result = BindRebuiltParameter(context, bindings, cell.source, parameter);
        if (result != CK_OK) return result;
    }
    return CK_OK;
}

template <typename State, typename Resolver>
CKERROR RestoreArrayShortcuts(CKDataArray *array, const State &state, Resolver resolve) {
    if (!array) return CKERR_INVALIDPARAMETER;
    for (const auto &cell : state.cells) {
        if (cell.type != CKARRAYTYPE_PARAMETER || !cell.parameter || cell.owned) continue;
        auto *source = CKParameter::Cast(resolve(cell.source));
        if (!source || source->IsToBeDeleted() ||
            source->GetGUID() != cell.parameter->GetGUID()) return CKERR_INVALIDOBJECT;
        auto *element = array->GetElement(cell.row, cell.column);
        if (element && reinterpret_cast<CKParameter *>(*element) == source) continue;
        if (!array->PasteShortcut(cell.row, cell.column, source)) return CKERR_INVALIDOBJECT;
    }
    return CK_OK;
}
} // namespace PracticeTimeline
