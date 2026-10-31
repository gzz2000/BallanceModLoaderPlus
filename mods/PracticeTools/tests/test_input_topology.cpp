#include "../InputTopology.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
extern void SetProcessorSpecific_FunctionsPtr();
static unsigned checks;
static void Require(bool ok, const char *what) {
    ++checks;
    if (!ok) { std::fprintf(stderr, "FAIL %s\n", what); std::exit(1); }
}
int main() {
    SetProcessorSpecific_FunctionsPtr();
    Require(CKStartUp() == CK_OK, "startup");
    CKContext *ctx = nullptr;
    Require(CKCreateContext(&ctx, nullptr, 0, 0) == CK_OK, "context");
    auto *a = ctx->CreateCKParameterIn("a", CKPGUID_INT);
    auto *b = ctx->CreateCKParameterIn("b", CKPGUID_INT);
    auto *c = ctx->CreateCKParameterIn("c", CKPGUID_INT);
    auto *d = ctx->CreateCKParameterIn("alias", CKPGUID_INT);
    auto *empty = ctx->CreateCKParameterIn("shared null", CKPGUID_INT);
    auto *value = ctx->CreateCKParameterOut("value", CKPGUID_INT);
    auto *wrongType = ctx->CreateCKParameterOut("wrong type", CKPGUID_VECTOR);
    int number = 123; value->SetValue(&number);
    auto resolve = [&](CK_ID id) { return ctx->GetObject(id); };
    std::vector<PracticeTimeline::InputState> saved = {
        {a->GetID(), b->GetID(), true, true},
        {b->GetID(), c->GetID(), true, true},
        {c->GetID(), value->GetID(), false, true},
        {d->GetID(), a->GetID(), true, false},
        {empty->GetID(), 0, true, true}};
    for (int repeat = 0; repeat < 12; ++repeat) {
        for (auto *p : {a,b,c,d,empty}) p->SetDirectSource(nullptr);
        Require(a->SetDirectSource(value) == CK_OK, "abandoned root");
        Require(b->ShareSourceWith(a) == CK_OK && c->ShareSourceWith(b) == CK_OK,
                "abandoned opposite chain");
        Require(a->ShareSourceWith(b) != CK_OK, "reproduce mixed-graph cycle rejection");
        Require(PracticeTimeline::RestoreInputTopology(saved, resolve) == CK_OK, "restore final DAG");
        Require(a->GetSharedSource() == b && b->GetSharedSource() == c &&
                c->GetDirectSource() == value && d->GetSharedSource() == a,
                "exact chain and fanout");
        Require(d->GetRealSource() == value && !d->IsEnabled(), "alias and disabled flag");
        Require((empty->GetObjectFlags() & CK_PARAMETERIN_SHARED) && !empty->GetSharedSource(),
                "shared-null flag preserved");
        std::rotate(saved.begin(), saved.begin() + 1, saved.end());
    }
    // Reject broken snapshots before changing even an unrelated input/flag.
    for (int kind = 0; kind < 8; ++kind) {
        auto bad = saved;
        auto &root = *std::find_if(bad.begin(), bad.end(), [&](const auto &s) {return s.id == c->GetID();});
        if (kind == 0) { root.shared = true; root.source = a->GetID(); } // cycle
        if (kind == 1) bad.push_back(bad[0]); // duplicate semantic owner
        if (kind == 2) root.source = 0xffffffff; // unresolved
        if (kind == 3) root.source = wrongType->GetID();
        if (kind == 4) root.source = a->GetID(); // direct source is an input
        if (kind == 5) { root.shared = true; root.source = value->GetID(); }
        if (kind == 6) bad.erase(std::find_if(bad.begin(), bad.end(), [&](const auto &s) {return s.id == b->GetID();}));
        if (kind == 7) root.id = value->GetID();
        Require(PracticeTimeline::RestoreInputTopology(bad, resolve) != CK_OK, "reject invalid graph");
        Require(a->GetSharedSource() == b && b->GetSharedSource() == c &&
                c->GetDirectSource() == value && d->GetRealSource() == value && !d->IsEnabled(),
                "preflight failure leaves graph and flags intact");
    }
    // Semantic IDs may resolve to a fresh owned cell rather than an old CK_ID.
    auto *replacement = ctx->CreateCKParameterOut("rebuilt cell", CKPGUID_INT);
    Require(PracticeTimeline::RestoreInputTopology(saved, [&](CK_ID id) -> CKObject * {
        return id == value->GetID() ? replacement : resolve(id);
    }) == CK_OK, "remapped direct source");
    Require(d->GetRealSource() == replacement, "every alias follows rebuilt semantic slot");

    std::vector<PracticeTimeline::InputState> longChain;
    std::vector<CKParameterIn *> inputs;
    for (int i = 0; i < 1100; ++i) inputs.push_back(ctx->CreateCKParameterIn("long DAG", CKPGUID_INT));
    for (size_t i = 0; i < inputs.size(); ++i)
        longChain.push_back({inputs[i]->GetID(), i + 1 < inputs.size() ? inputs[i+1]->GetID() : value->GetID(),
                            i + 1 < inputs.size(), true});
    Require(PracticeTimeline::RestoreInputTopology(longChain, resolve) == CK_OK, "long valid DAG avoids setter traversal limit");
    Require(inputs.front()->GetRealSource() == value, "long DAG resolves iteratively");
    Require(CKCloseContext(ctx) == CK_OK, "close");
    Require(CKShutdown() == CK_OK, "shutdown");
    std::printf("PASS %u input topology checks\n", checks);
}
