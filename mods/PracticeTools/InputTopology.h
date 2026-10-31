#pragma once
#include "CKAll.h"
#include <unordered_map>
#include <vector>

namespace PracticeTimeline {
struct InputState {
    CK_ID id = 0;
    CK_ID source = 0;
    bool shared = false;
    bool enabled = true;
};

// A saved DAG and the abandoned DAG can form a cycle while individual edges
// are replaced. ShareSourceWith rejects that intermediate cycle, leaving the
// abandoned edge (possibly pointing at a destroyed array cell) in place.
// Resolve and validate the whole saved graph, clear ALL old edges, then install
// the saved graph. No value evaluation, propagation or owner mutation occurs.
template <typename Resolver>
CKERROR RestoreInputTopology(const std::vector<InputState> &states, Resolver resolve) {
    struct Row {
        CKParameterIn *input;
        CKObject *source;
        bool shared, enabled;
    };
    std::vector<Row> rows;
    std::unordered_map<CKParameterIn *, size_t> indices;
    for (const auto &state : states) {
        auto *input = CKParameterIn::Cast(resolve(state.id));
        if (!input || input->IsToBeDeleted() || indices.count(input)) return CKERR_INVALIDOBJECT;
        CKObject *source = state.source ? resolve(state.source) : nullptr;
        if (state.source && (!source || source->IsToBeDeleted())) return CKERR_INVALIDOBJECT;
        if (source) {
            auto *shared = CKParameterIn::Cast(source);
            auto *direct = CKParameter::Cast(source);
            if ((state.shared && !shared) || (!state.shared && !direct) ||
                source->GetCKContext() != input->GetCKContext()) return CKERR_INVALIDOBJECT;
            // Mirror CK2's source compatibility check before clearing anything.
            auto *manager = input->GetCKContext()->GetParameterManager();
            const auto inputType = input->GetType();
            const auto sourceType = state.shared ? shared->GetType() : direct->GetType();
            const auto *a = manager->GetParameterTypeDescription(inputType);
            const auto *b = manager->GetParameterTypeDescription(sourceType);
            if (!a || !b ||
                (!manager->IsDerivedFrom(sourceType, inputType) &&
                 !manager->IsDerivedFrom(inputType, sourceType)) ||
                (a->Cid && !CKIsChildClassOf(a->Cid, b->Cid) &&
                 !CKIsChildClassOf(b->Cid, a->Cid))) return CKERR_INCOMPATIBLEPARAMETERS;
        }
        indices.emplace(input, rows.size());
        rows.push_back({input, source, state.shared, state.enabled});
    }
    std::vector<std::vector<size_t>> consumers(rows.size());
    std::vector<size_t> dependencies(rows.size(), 0), order;
    for (size_t i = 0; i < rows.size(); ++i) {
        const auto &row = rows[i];
        if (!row.shared || !row.source) continue;
        auto found = indices.find(CKParameterIn::Cast(row.source));
        if (found == indices.end()) return CKERR_INVALIDOBJECT;
        consumers[found->second].push_back(i);
        ++dependencies[i];
    }
    for (size_t i = 0; i < rows.size(); ++i) if (!dependencies[i]) order.push_back(i);
    for (size_t i = 0; i < order.size(); ++i)
        for (auto consumer : consumers[order[i]])
            if (!--dependencies[consumer]) order.push_back(consumer);
    if (order.size() != rows.size()) return CKERR_INVALIDPARAMETER;

    for (const auto &row : rows) {
        const auto result = row.input->SetDirectSource(nullptr);
        if (result != CK_OK) return result;
    }
    // Consumers first: each shared source is still disconnected when linked.
    // This also avoids the setter's 1024-step traversal limit for a long but
    // valid DAG. GetRealSource resolves those chains iteratively in CK2.
    for (auto it = order.rbegin(); it != order.rend(); ++it) {
        const auto &row = rows[*it];
        const auto result = row.shared
            ? row.input->ShareSourceWith(CKParameterIn::Cast(row.source))
            : row.input->SetDirectSource(CKParameter::Cast(row.source));
        if (result != CK_OK) return result;
        row.input->Enable(row.enabled ? TRUE : FALSE);
    }
    return CK_OK;
}
} // namespace PracticeTimeline
