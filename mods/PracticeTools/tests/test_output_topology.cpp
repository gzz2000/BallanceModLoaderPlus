#include "../OutputTopology.h"
#include <cstdio>
#include <cstdlib>
extern void SetProcessorSpecific_FunctionsPtr();
static unsigned checks;
static void Require(bool ok, const char *what) {
    ++checks;
    if (!ok) { std::fprintf(stderr, "FAIL %s\n", what); std::exit(1); }
}
static int Read(CKParameter *p) {
    int value = 0; Require(p->GetValue(&value, FALSE) == CK_OK, "read"); return value;
}
static void Set(CKParameter *p, int value) { Require(p->SetValue(&value) == CK_OK, "set"); }
int main() {
    SetProcessorSpecific_FunctionsPtr();
    Require(CKStartUp() == CK_OK, "startup");
    CKContext *ctx = nullptr;
    Require(CKCreateContext(&ctx, nullptr, 0, 0) == CK_OK, "context");
    auto *a = ctx->CreateCKParameterOut("first destination", CKPGUID_INT);
    auto *b = ctx->CreateCKParameterOut("second destination", CKPGUID_INT);
    auto *c = ctx->CreateCKParameterOut("source", CKPGUID_INT);
    auto resolve = [&](CK_ID id) { return ctx->GetObject(id); };
    std::vector<PracticeTimeline::OutputState> saved = {
        {a->GetID(), {}}, {b->GetID(), {}}, {c->GetID(), {b->GetID(), a->GetID()}}};
    for (unsigned i = 0; i < 8; ++i) {
        for (auto *p : {a, b, c}) p->RemoveAllDestinations();
        Require(a->AddDestination(c) == CK_OK, "abandoned edge points opposite saved graph");
        Require(c->AddDestination(b) == CK_OK, "abandoned chain");
        PracticeTimeline::OutputTopologyRestore graph;
        Require(graph.Prepare(saved, resolve) == CK_OK, "preflight final graph independent of current graph");
        Require(a->GetDestination(0) == c, "prepare does not mutate");
        graph.Suspend();
        Set(a, 11); Set(b, 22); Set(c, 33);
        Require(graph.Commit() == CK_OK, "commit");
        Require(Read(a) == 11 && Read(b) == 22 && Read(c) == 33, "distinct connected values preserved");
        Require(!a->GetDestinationCount() && c->GetDestination(0) == b && c->GetDestination(1) == a,
                "exact ordered graph");
        Set(c, 44);
        Require(Read(a) == 44 && Read(b) == 44, "normal live propagation resumes");
    }
    for (int kind = 0; kind < 5; ++kind) {
        auto bad = saved;
        if (kind == 0) bad[0].destinations = {c->GetID()}; // cycle
        if (kind == 1) bad[2].destinations.push_back(a->GetID()); // duplicate
        if (kind == 2) bad[2].destinations.push_back(0); // unresolved
        if (kind == 3) bad.push_back(saved[0]); // aliased source
        if (kind == 4) bad.erase(bad.begin()); // omitted output destination
        PracticeTimeline::OutputTopologyRestore graph;
        Require(graph.Prepare(bad, resolve) != CK_OK, "reject malformed snapshot before mutation");
        Require(c->GetDestination(0) == b && c->GetDestination(1) == a && Read(a) == 44,
                "rejection leaves current graph and values intact");
    }
    {
        PracticeTimeline::OutputTopologyRestore graph;
        Require(graph.Prepare(saved, resolve) == CK_OK, "prepare rollback");
        graph.Suspend(); // Simulate abort before a value/commit phase.
    }
    Require(c->GetDestination(0) == b && c->GetDestination(1) == a,
            "early exit restores abandoned graph");

    // Structured copiers recursively invoke child CopyValue; suppressing only
    // the parent's virtual function would still overwrite this child target.
    CKGUID guid(0x70f07391, 0x2da81064);
    XArray<CKGUID> types; types.PushBack(CKPGUID_INT);
    Require(ctx->GetParameterManager()->RegisterNewStructure(guid, "fixture structure", "child", types) == CK_OK,
            "register structure");
    auto *structure = ctx->CreateCKParameterOut("structure", guid);
    auto *copy = ctx->CreateCKParameterOut("saved structure", guid);
    auto child = [&](CKParameter *p) {
        auto *ids = static_cast<CK_ID *>(p->GetReadDataPtr(FALSE));
        return CKParameterOut::Cast(ctx->GetObject(ids[0]));
    };
    auto *member = child(structure); auto *savedMember = child(copy);
    Set(savedMember, 123); Set(a, 17);
    Require(member->AddDestination(a) == CK_OK, "structured child edge");
    auto graphState = saved;
    graphState.push_back({structure->GetID(), {}});
    graphState.push_back({member->GetID(), {a->GetID()}});
    {
        PracticeTimeline::OutputTopologyRestore graph;
        Require(graph.Prepare(graphState, resolve) == CK_OK, "structured graph preflight");
        graph.Suspend();
        Require(structure->CopyValue(copy, FALSE) == CK_OK, "typed structure copy while detached");
        Require(graph.Commit() == CK_OK, "structured graph commit");
    }
    Require(Read(member) == 123 && Read(a) == 17, "child copy leaves independent target unchanged");
    Set(member, 91); Require(Read(a) == 91, "child propagation resumes");
    // A saved array-owned destination is resolved by its semantic slot after
    // reconstruction; restoring the old numeric identity would lose the edge.
    auto *array = CKDataArray::Cast(ctx->CreateObject(CKCID_DATAARRAY, "owned destination"));
    array->InsertColumn(-1, CKARRAYTYPE_PARAMETER, "cell", CKPGUID_INT);
    array->AddRow();
    auto cell = [&]() { return reinterpret_cast<CKParameterOut *>(*array->GetElement(0, 0)); };
    auto *oldCell = cell(); const CK_ID savedCellId = oldCell->GetID();
    auto *driver = ctx->CreateCKParameterOut("array driver", CKPGUID_INT);
    Require(driver->AddDestination(oldCell) == CK_OK, "connect array cell");
    std::vector<PracticeTimeline::OutputState> owned = {
        {savedCellId, {}}, {driver->GetID(), {savedCellId}}};
    array->RemoveRow(0); array->AddRow();
    auto *newCell = cell();
    auto rebound = [&](CK_ID id) -> CKObject * {
        return id == savedCellId ? newCell : ctx->GetObject(id);
    };
    {
        PracticeTimeline::OutputTopologyRestore graph;
        Require(graph.Prepare(owned, rebound) == CK_OK, "resolve reconstructed cell before mutation");
        graph.Suspend(); Set(newCell, 5); Set(driver, 9);
        Require(graph.Commit() == CK_OK, "commit reconstructed graph");
    }
    Require(driver->GetDestination(0) == newCell && Read(newCell) == 5,
            "saved destination binds recreated cell without propagating");
    Set(driver, 72); Require(Read(newCell) == 72, "recreated destination remains live");

    CKCloseContext(ctx); CKShutdown();
    std::printf("PASS %u checks\n", checks);
}
