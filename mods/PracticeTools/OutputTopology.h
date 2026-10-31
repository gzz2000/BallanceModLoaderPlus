#pragma once
#include "CKAll.h"
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace PracticeTimeline {
struct OutputState {
    CK_ID id = 0;
    std::vector<CK_ID> destinations;
};

// CopyValue(FALSE) disables source evaluation, not destination propagation.
// Keep every output disconnected throughout typed value restoration: custom
// copiers can call SetValue and structured copiers recursively copy children.
// AddDestination does not propagate, so unequal connected values are preserved.
class OutputTopologyRestore {
    struct Row {
        CKParameterOut *output;
        std::vector<CKParameter *> saved;
        std::vector<CKParameter *> previous;
    };
    std::vector<Row> m_Rows;
    bool m_Suspended = false;
    void Clear() {
        for (const auto &row : m_Rows) row.output->RemoveAllDestinations();
    }
    CKERROR Install(bool saved) {
        for (const auto &row : m_Rows) {
            for (auto *destination : saved ? row.saved : row.previous) {
                // A legal graph can deliberately connect types with CheckType=FALSE.
                const auto result = row.output->AddDestination(destination, FALSE);
                if (result != CK_OK) return result;
            }
        }
        return CK_OK;
    }
public:
    OutputTopologyRestore() = default;
    OutputTopologyRestore(const OutputTopologyRestore &) = delete;
    OutputTopologyRestore &operator=(const OutputTopologyRestore &) = delete;
    ~OutputTopologyRestore() {
        if (m_Suspended) { Clear(); Install(false); }
    }

    template <typename Resolver>
    CKERROR Prepare(const std::vector<OutputState> &states, Resolver resolve) {
        if (m_Suspended) return CKERR_INVALIDOPERATION;
        m_Rows.clear();
        std::unordered_map<CKParameterOut *, size_t> indices;
        for (const auto &state : states) {
            auto *output = CKParameterOut::Cast(resolve(state.id));
            if (!output || output->IsToBeDeleted() || indices.count(output))
                return CKERR_INVALIDOBJECT;
            indices.emplace(output, m_Rows.size());
            Row row{output, {}, {}};
            std::unordered_set<CKParameter *> unique;
            for (auto id : state.destinations) {
                auto *target = CKParameter::Cast(resolve(id));
                if (!target || target->IsToBeDeleted() ||
                    (target->GetObjectFlags() & CK_PARAMETERIN_THIS) ||
                    !unique.insert(target).second) return CKERR_INVALIDOBJECT;
                row.saved.push_back(target);
            }
            for (int i = 0; i < output->GetDestinationCount(); ++i) {
                auto *target = output->GetDestination(i);
                if (!target || target->IsToBeDeleted()) return CKERR_INVALIDOBJECT;
                row.previous.push_back(target);
            }
            m_Rows.push_back(std::move(row));
        }
        // Validate the final graph, not a partially restored graph mixed with
        // abandoned edges. Iterative cycle detection also handles long chains.
        std::vector<size_t> incoming(m_Rows.size(), 0), ready;
        for (const auto &row : m_Rows) for (auto *target : row.saved) {
            auto *output = CKParameterOut::Cast(target);
            if (!output) continue;
            const auto found = indices.find(output);
            if (found == indices.end()) return CKERR_INVALIDOBJECT;
            ++incoming[found->second];
        }
        for (size_t i = 0; i < incoming.size(); ++i)
            if (!incoming[i]) ready.push_back(i);
        for (size_t i = 0; i < ready.size(); ++i) {
            for (auto *target : m_Rows[ready[i]].saved) {
                auto *output = CKParameterOut::Cast(target);
                if (output && !--incoming[indices.at(output)]) ready.push_back(indices.at(output));
            }
        }
        return ready.size() == m_Rows.size() ? CK_OK : CKERR_INVALIDPARAMETER;
    }
    void Suspend() { Clear(); m_Suspended = true; }
    CKERROR Commit() {
        if (!m_Suspended) return CKERR_INVALIDOPERATION;
        const auto result = Install(true);
        if (result == CK_OK) m_Suspended = false;
        return result;
    }
};
} // namespace PracticeTimeline
