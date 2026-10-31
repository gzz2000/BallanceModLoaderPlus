#pragma once

#include "CKAll.h"
#include <unordered_map>
#include <vector>
#if defined(__APPLE__) || defined(PRACTICE_BUILD_STATIC)
#include "CKObjectLifetime.h"
#endif

namespace PracticeTimeline {
using Identities = std::unordered_map<CK_ID, CKObject *>;
#if defined(__APPLE__) || defined(PRACTICE_BUILD_STATIC)
using LifetimeTokens = std::unordered_map<CK_ID, std::uint64_t>;
inline bool MatchesSavedObject(CKContext *context, CK_ID id,
                               const Identities &identities, const LifetimeTokens &tokens) {
    if (!context || !id) return false;
    const auto saved = identities.find(id);
    const auto token = tokens.find(id);
    return saved != identities.end() && token != tokens.end() && token->second &&
           context->GetObject(id) == saved->second &&
           CKGetObjectLifetimeToken(context, id) == token->second;
}
#endif

// Caller supplies the complete eligible set, excluding saved world objects,
// private snapshot storage and objects owned by separate semantic restorers.
// Do not use CK_DEPENDENCIES_FULL: a newly created entity can share a saved
// material/mesh. Even NONE includes mandatory script/attribute ownership, so
// preview that closure and reject crossing the supplied ownership boundary.
inline CKERROR DeleteNewObjects(CKContext *context, const Identities &eligible,
                                std::size_t &removed) {
    removed = 0;
    if (!context) return CKERR_INVALIDPARAMETER;
    if (eligible.empty()) return CK_OK;
    if (context->m_DeferDestroyObjects ||
        context->m_DependenciesContext.GetDependenciesMap().Size())
        return CKERR_INVALIDPARAMETER;

    CKDependencies options; // NONE: only unavoidable ownership dependencies
    CKDependenciesContext preview(context);
    preview.SetOperationMode(CK_DEPENDENCIES_DELETE);
    preview.StartDependencies(&options);
    for (const auto &entry : eligible) {
        CKObject *object = context->GetObject(entry.first);
        if (!object || object != entry.second || object->IsToBeDeleted())
            return CKERR_INVALIDOBJECT;
        const CKERROR result = object->PrepareDependencies(preview);
        if (result != CK_OK && result != CKERR_ALREADYPRESENT)
            return result;
    }
    preview.StopDependencies();
    const auto &dependencies = preview.GetDependenciesMap();
    std::vector<CK_ID> ids;
    ids.reserve(dependencies.Size());
    for (auto it = dependencies.Begin(); it != dependencies.End(); ++it) {
        const CK_ID id = it.GetKey();
        const auto allowed = eligible.find(id);
        if (allowed == eligible.end() || context->GetObject(id) != allowed->second)
            return CKERR_INVALIDOBJECT;
        ids.push_back(id);
    }
    if (ids.size() != eligible.size()) return CKERR_INVALIDOBJECT;

    // One normal notified sequence removes scene/group/render references and
    // owned runtime state before the saved state is applied. Never use direct
    // delete, NONOTIFY, or FREEID here. No simulation/callback dispatch is allowed
    // between this preview and commit, or between commit and the enclosing restore.
    const CKERROR result = context->DestroyObjects(ids.data(), int(ids.size()), 0, &options);
    if (result != CK_OK) return result;
    for (CK_ID id : ids) {
        if (context->GetObject(id)) return CKERR_INVALIDOBJECT;
        ++removed;
    }
    return CK_OK;
}
} // namespace PracticeTimeline
