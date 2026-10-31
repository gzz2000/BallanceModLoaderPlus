#pragma once

#include "CKAll.h"
#include <cstddef>

namespace PracticeTimeline {
// State supplies the saved rows, columns, keyColumn and columnStates. Keep
// shape repair separate from cell values and semantic parameter rebinding.
template <typename State>
CKERROR RestoreArrayShape(CKDataArray *array, const State &state) {
    if (!array || state.rows < 0 || state.columns < 0 ||
        state.columnStates.size() != static_cast<std::size_t>(state.columns))
        return CKERR_INVALIDPARAMETER;
    while (array->GetColumnCount() > state.columns) {
        const int before = array->GetColumnCount();
        array->RemoveColumn(before - 1);
        if (array->GetColumnCount() != before - 1) return CKERR_INVALIDOBJECT;
    }
    while (array->GetColumnCount() < state.columns) {
        const int before = array->GetColumnCount();
        const auto &column = state.columnStates[before];
        // CKDataArray rejects index == column count. -1 is the append sentinel.
        array->InsertColumn(-1, column.type,
            const_cast<char *>(column.name.c_str()), column.parameterGuid);
        if (array->GetColumnCount() != before + 1) return CKERR_INVALIDOBJECT;
    }
    for (int i = 0; i < state.columns; ++i) {
        const auto &column = state.columnStates[i];
        if (array->GetColumnType(i) != column.type ||
            (column.type == CKARRAYTYPE_PARAMETER &&
             array->GetColumnParameterGuid(i) != column.parameterGuid))
            array->SetColumnType(i, column.type, column.parameterGuid);
        if (array->GetColumnType(i) != column.type ||
            (column.type == CKARRAYTYPE_PARAMETER &&
             array->GetColumnParameterGuid(i) != column.parameterGuid))
            return CKERR_INVALIDOBJECT;
        array->SetColumnName(i, const_cast<char *>(column.name.c_str()));
    }
    array->SetKeyColumn(state.keyColumn);
    while (array->GetRowCount() > state.rows) {
        const int before = array->GetRowCount();
        array->RemoveRow(before - 1);
        if (array->GetRowCount() != before - 1) return CKERR_INVALIDOBJECT;
    }
    while (array->GetRowCount() < state.rows) {
        const int before = array->GetRowCount();
        array->AddRow();
        if (array->GetRowCount() != before + 1) return CKERR_INVALIDOBJECT;
    }
    return CK_OK;
}
} // namespace PracticeTimeline
