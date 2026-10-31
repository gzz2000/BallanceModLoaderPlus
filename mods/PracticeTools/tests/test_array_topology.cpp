#include "../ArrayTopology.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
extern void SetProcessorSpecific_FunctionsPtr();
static unsigned checks;
static void Require(bool value, const char *message) {
    ++checks;
    if (!value) { std::fprintf(stderr, "FAIL %s\n", message); std::exit(1); }
}
struct Shape {
    struct Column {
        CK_ARRAYTYPE type;
        CKGUID parameterGuid;
        std::string name;
    };
    int rows = 0, columns = 0, keyColumn = -1;
    std::vector<Column> columnStates;
};
static void Verify(CKDataArray *array, const Shape &shape) {
    Require(array->GetColumnCount() == shape.columns, "column count");
    Require(array->GetRowCount() == shape.rows, "row count");
    Require(array->GetKeyColumn() == shape.keyColumn, "key column");
    for (int c = 0; c < shape.columns; ++c) {
        const auto &column = shape.columnStates[c];
        Require(array->GetColumnType(c) == column.type, "column type");
        Require(std::strcmp(array->GetColumnName(c), column.name.c_str()) == 0,
                "column name");
        if (column.type != CKARRAYTYPE_PARAMETER) continue;
        Require(array->GetColumnParameterGuid(c) == column.parameterGuid, "column GUID");
        for (int r = 0; r < shape.rows; ++r) {
            auto *element = array->GetElement(r, c);
            auto *parameter = element ? reinterpret_cast<CKParameterOut *>(*element) : nullptr;
            Require(parameter && parameter->GetOwner() == array &&
                    parameter->GetGUID() == column.parameterGuid, "owned typed cell");
        }
    }
}
int main() {
    SetProcessorSpecific_FunctionsPtr();
    Require(CKStartUp() == CK_OK, "startup");
    CKContext *ctx = nullptr;
    Require(CKCreateContext(&ctx, nullptr, 0, 0) == CK_OK, "context");
    auto *array = CKDataArray::Cast(ctx->CreateObject(CKCID_DATAARRAY, "snapshot array"));
    Require(array != nullptr, "array");
    Shape saved{3, 4, 0, {
        {CKARRAYTYPE_INT, CKGUID(), "key"},
        {CKARRAYTYPE_PARAMETER, CKPGUID_FLOAT, "float parameter"},
        {CKARRAYTYPE_STRING, CKGUID(), "label"},
        {CKARRAYTYPE_OBJECT, CKGUID(), "object"}}};
    // This is the exact legacy call used by RestoreState. It does not append.
    array->InsertColumn(array->GetColumnCount(), CKARRAYTYPE_INT, "rejected append");
    Require(array->GetColumnCount() == 0, "legacy append makes no progress");
    Require(PracticeTimeline::RestoreArrayShape(array, saved) == CK_OK, "empty to saved shape");
    Verify(array, saved);
    for (unsigned iteration = 0; iteration < 12; ++iteration) {
        array->RemoveColumn(array->GetColumnCount() - 1);
        array->RemoveColumn(0);
        array->RemoveRow(0);
        Require(PracticeTimeline::RestoreArrayShape(array, saved) == CK_OK,
                "restore removed and shifted columns/rows");
        Verify(array, saved);
        array->InsertColumn(-1, CKARRAYTYPE_STRING, "extra");
        array->AddRow(); array->AddRow();
        array->SetColumnType(1, CKARRAYTYPE_PARAMETER, CKPGUID_INT);
        Require(PracticeTimeline::RestoreArrayShape(array, saved) == CK_OK,
                "remove extras and restore changed parameter type");
        Verify(array, saved);
        auto *element = array->GetElement(0, 1);
        auto *parameter = reinterpret_cast<CKParameterOut *>(*element);
        float value = 123.5f;
        Require(parameter->SetValue(&value) == CK_OK, "set existing value");
        Require(PracticeTimeline::RestoreArrayShape(array, saved) == CK_OK, "idempotent shape");
        Require(reinterpret_cast<CKParameterOut *>(*array->GetElement(0, 1)) == parameter,
                "unchanged shape preserves parameter identity");
        float observed = 0;
        Require(parameter->GetValue(&observed, FALSE) == CK_OK && observed == value,
                "shape does not replace cell values");
    }
    Shape empty;
    Require(PracticeTimeline::RestoreArrayShape(array, empty) == CK_OK, "restore empty shape");
    Verify(array, empty);
    Shape invalid = saved; invalid.columnStates.pop_back();
    Require(PracticeTimeline::RestoreArrayShape(array, invalid) == CKERR_INVALIDPARAMETER,
            "reject incomplete column descriptors before mutation");
    Verify(array, empty);
    Require(PracticeTimeline::RestoreArrayShape(nullptr, saved) == CKERR_INVALIDPARAMETER,
            "reject null owner");
    Require(CKCloseContext(ctx) == CK_OK && CKShutdown() == CK_OK, "shutdown");
    std::printf("RESULT checks=%u failures=0\n", checks);
}
