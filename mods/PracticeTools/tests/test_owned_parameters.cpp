#include "../TimelineObjects.h"
#include "../OwnedParameters.h"
#include <vector>
#include <cstdio>
#include <cstdlib>
extern void SetProcessorSpecific_FunctionsPtr();
static unsigned checks;
static void Require(bool ok, const char *what) {
    ++checks;
    if (!ok) { std::fprintf(stderr, "FAIL %s\n", what); std::exit(1); }
}
static CKParameterOut *Cell(CKDataArray *array, int row) {
    auto *element = array->GetElement(row, 0);
    return element ? reinterpret_cast<CKParameterOut *>(*element) : nullptr;
}
static void Set(CKParameter *p, int value) {
    Require(p && p->SetValue(&value) == CK_OK, "set value");
}
static int Read(CKParameterIn *p) {
    int result = -1;
    Require(p->GetValue(&result) == CK_OK, "read connected value");
    return result;
}
struct ArrayState {
    struct CellState {
        int row = 0, column = 0;
        CK_ARRAYTYPE type = CKARRAYTYPE_PARAMETER;
        CK_ID source = 0;
        bool owned = true;
        CKParameterOut *parameter = nullptr;
    };
    std::vector<CellState> cells;
};
int main() {
    SetProcessorSpecific_FunctionsPtr();
    Require(CKStartUp() == CK_OK, "startup");
    CKContext *ctx = nullptr;
    Require(CKCreateContext(&ctx, nullptr, 0, 0) == CK_OK, "context");
    auto *array = CKDataArray::Cast(ctx->CreateObject(CKCID_DATAARRAY, "queue"));
    Require(array != nullptr, "array");
    array->InsertColumn(-1, CKARRAYTYPE_PARAMETER, "value", CKPGUID_INT);
    Require(array->GetColumnCount() == 1, "append column uses -1 sentinel");
    array->AddRow(); array->AddRow();
    auto *a = Cell(array, 0); auto *b = Cell(array, 1);
    Require(a && b, "two owned cells");
    Set(a, 11); Set(b, 22);
    auto *inA = ctx->CreateCKParameterIn("first row", CKPGUID_INT);
    auto *inB = ctx->CreateCKParameterIn("second row", CKPGUID_INT);
    Require(inA && inB, "inputs");
    Require(inA->SetDirectSource(a) == CK_OK && inB->SetDirectSource(b) == CK_OK,
            "connect saved inputs");
    const CK_ID aid = a->GetID(), bid = b->GetID();
    PracticeTimeline::Identities identities{{aid, a}, {bid, b}};
    PracticeTimeline::LifetimeTokens tokens{
        {aid, CKGetObjectLifetimeToken(ctx, aid)}, {bid, CKGetObjectLifetimeToken(ctx, bid)}};
    auto resolve = [&](CK_ID id) -> CKParameter * {
        return PracticeTimeline::MatchesSavedObject(ctx, id, identities, tokens)
            ? CKParameter::Cast(ctx->GetObject(id)) : nullptr;
    };
    array->RemoveRow(0);
    Require(Cell(array, 0) == b, "surviving row shifted");
    // Current restore only reconciles row counts. It then resolves input
    // sources by old lifetime, and copies saved values by row/column.
    array->AddRow();
    Require(inA->SetDirectSource(resolve(aid)) == CK_OK &&
            inB->SetDirectSource(resolve(bid)) == CK_OK, "current restore wiring");
    Set(Cell(array, 0), 11); Set(Cell(array, 1), 22);
    Require(inA->GetDirectSource() == nullptr, "reproduced lost recreated-cell input");
    Require(Read(inB) == 11, "reproduced surviving identity follows wrong restored row");
    std::printf("REPRO array removed source disconnected; surviving source reads 11, expected 22\n");
    auto *copyA = ctx->CreateCKParameterOut("saved A", CKPGUID_INT);
    auto *copyB = ctx->CreateCKParameterOut("saved B", CKPGUID_INT);
    Require(copyA && copyB, "saved copies"); Set(copyA, 11); Set(copyB, 22);
    ArrayState saved{{{0, 0, CKARRAYTYPE_PARAMETER, aid, true, copyA},
                      {1, 0, CKARRAYTYPE_PARAMETER, bid, true, copyB}}};
    PracticeTimeline::RebuiltParameters rebuilt;
    Require(PracticeTimeline::BindArrayParameters(ctx, array, saved, rebuilt) == CK_OK,
            "production semantic binding");
    auto rebound = [&](CK_ID id) -> CKObject * {
        auto it = rebuilt.find(id);
        return it == rebuilt.end() ? resolve(id) : it->second;
    };
    Require(inA->SetDirectSource(CKParameter::Cast(rebound(aid))) == CK_OK &&
            inB->SetDirectSource(CKParameter::Cast(rebound(bid))) == CK_OK, "semantic slot control");
    Require(Read(inA) == 11 && Read(inB) == 22, "semantic array control values");

    auto *owner = CKBeObject::Cast(ctx->CreateObject(CKCID_BEOBJECT, "attribute owner"));
    Require(owner != nullptr, "attribute owner");
    const auto type = ctx->GetAttributeManager()->RegisterNewAttributeType(
        "checkpoint fixture", CKPGUID_INT);
    Require(type >= 0 && owner->SetAttribute(type), "attribute created");
    auto *attr = owner->GetAttributeParameter(type);
    Require(attr != nullptr, "attribute parameter");
    Set(attr, 33);
    auto *inAttr = ctx->CreateCKParameterIn("attribute consumer", CKPGUID_INT);
    Require(inAttr && inAttr->SetDirectSource(attr) == CK_OK, "attribute consumer");
    const auto attrId = attr->GetID();
    identities[attrId] = attr; tokens[attrId] = CKGetObjectLifetimeToken(ctx, attrId);
    Require(owner->RemoveAttribute(type), "remove attribute");
    Require(inAttr->SetDirectSource(resolve(attrId)) == CK_OK, "current attribute wiring order");
    Require(owner->SetAttribute(type), "reconstruct attribute");
    Set(owner->GetAttributeParameter(type), 33);
    Require(inAttr->GetDirectSource() == nullptr, "reproduced disconnected rebuilt attribute");
    Require(PracticeTimeline::BindRebuiltParameter(ctx, rebuilt, attrId,
                owner->GetAttributeParameter(type)) == CK_OK, "bind rebuilt attribute");
    Require(inAttr->SetDirectSource(CKParameter::Cast(rebound(attrId))) == CK_OK &&
            Read(inAttr) == 33, "semantic attribute control");
    std::printf("REPRO attribute value restored to 33 but input disconnected until rebound\n");
    auto *shortcutArray = CKDataArray::Cast(ctx->CreateObject(CKCID_DATAARRAY, "shortcuts"));
    Require(shortcutArray != nullptr, "shortcut array");
    shortcutArray->InsertColumn(-1, CKARRAYTYPE_PARAMETER, "shared", CKPGUID_INT);
    shortcutArray->AddRow(); shortcutArray->AddRow();
    // Saved external aliases target an owned array cell and an attribute.
    ArrayState shortcuts{{{0, 0, CKARRAYTYPE_PARAMETER, bid, false, copyB},
                          {1, 0, CKARRAYTYPE_PARAMETER, attrId, false, copyA}}};
    for (unsigned round = 0; round < 12; ++round) {
        // Simulate consumption, row shifting, attribute removal and an owned
        // slot temporarily being replaced by an unrelated external shortcut.
        array->RemoveRow(0); array->AddRow();
        Require(owner->RemoveAttribute(type) && owner->SetAttribute(type), "rebuild attribute again");
        Require(array->PasteShortcut(0, 0, copyA), "abandoned shortcut in saved owned slot");
        rebuilt.clear();
        Require(PracticeTimeline::BindArrayParameters(ctx, array, saved, rebuilt) == CK_OK,
                "rebind owned cells after repeated reconstruction");
        Require(PracticeTimeline::BindRebuiltParameter(ctx, rebuilt, attrId,
                    owner->GetAttributeParameter(type)) == CK_OK, "rebind attribute again");
        Require(PracticeTimeline::RestoreArrayShortcuts(shortcutArray, shortcuts, rebound) == CK_OK,
                "restore cross-owner shortcuts after all bindings");
        Require(PracticeTimeline::RestoreArrayShortcuts(shortcutArray, shortcuts, rebound) == CK_OK,
                "idempotent shortcut restore");
        Set(Cell(array, 0), 11); Set(Cell(array, 1), 22);
        Set(owner->GetAttributeParameter(type), 33);
        Require(inA->SetDirectSource(CKParameter::Cast(rebound(aid))) == CK_OK &&
                inB->SetDirectSource(CKParameter::Cast(rebound(bid))) == CK_OK &&
                inAttr->SetDirectSource(CKParameter::Cast(rebound(attrId))) == CK_OK,
                "rewire inputs after structural restoration");
        Require(Read(inA) == 11 && Read(inB) == 22 && Read(inAttr) == 33,
                "all consumer values restored");
        Require(Cell(shortcutArray, 0) == Cell(array, 1) &&
                Cell(shortcutArray, 1) == owner->GetAttributeParameter(type),
                "aliases preserved across owners");
        Set(Cell(array, 1), 44); int sharedValue = 0;
        Require(Cell(shortcutArray, 0)->GetValue(&sharedValue, FALSE) == CK_OK && sharedValue == 44,
                "shortcut observes live source changes");
    }
    Require(PracticeTimeline::BindRebuiltParameter(ctx, rebuilt, aid, copyB) != CK_OK,
            "reject conflicting saved identity");
    Require(CKCloseContext(ctx) == CK_OK && CKShutdown() == CK_OK, "shutdown");
    std::printf("RESULT checks=%u failures=0 reproduced_legacy_defects=3 repeated_restores=12\n", checks);
}
