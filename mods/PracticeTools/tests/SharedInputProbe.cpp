#include <BML/BMLAll.h>
#include <dlfcn.h>
#include <array>
#include <cstdio>
#include <cstdlib>

MOD_EXPORT IMod *BMLEntry(IBML *);
MOD_EXPORT void BMLExit(IMod *);
class SharedInputProbe final : public IMod {
public:
    explicit SharedInputProbe(IBML *b) : IMod(b) {}
    const char *GetID() override { return "SharedInputProbe"; }
    const char *GetVersion() override { return "1.0"; }
    const char *GetName() override { return "Shared input rewind regression"; }
    const char *GetAuthor() override { return "gzz2000"; }
    const char *GetDescription() override { return "Test-only actual PracticeTools topology restoration"; }
    DECLARE_BML_VERSION;
    unsigned ticks = 0, checks = 0, failures = 0;
    bool done = false;
    FILE *log = nullptr;
    void check(bool ok, const char *what) {
        ++checks; if (!ok) ++failures;
        fprintf(log, "%s %s\n", ok ? "PASS" : "FAIL", what); fflush(log);
    }
    void OnProcess() override {
        if (!getenv("BALLANCE_CAPTURE_PROBE_LOG") || !getenv("BALLANCE_CAPTURE_PROBE_PRACTICE")) return;
        if (done || !m_BML->IsPlaying() || ++ticks < 4000) return;
        done = true; log = fopen(getenv("BALLANCE_CAPTURE_PROBE_LOG"), "w"); if (!log) return;
        void *lib = dlopen(getenv("BALLANCE_CAPTURE_PROBE_PRACTICE"), RTLD_NOW | RTLD_LOCAL);
        auto capture = lib ? reinterpret_cast<int(*)()>(dlsym(lib, "BML_PracticeCaptureState")) : nullptr;
        auto restore = lib ? reinterpret_cast<int(*)()>(dlsym(lib, "BML_PracticeRestoreState")) : nullptr;
        check(capture && restore, "resolve actual API");
        if (capture && restore) {
            auto *ctx = m_BML->GetCKContext();
            std::array<CKParameterIn *, 5> inputs{};
            for (auto &p : inputs) p = ctx->CreateCKParameterIn("shared rewind fixture", CKPGUID_INT);
            auto *a = CKDataArray::Cast(ctx->CreateObject(CKCID_DATAARRAY, "shared input cells"));
            a->InsertColumn(-1, CKARRAYTYPE_PARAMETER, "value", CKPGUID_INT);
            a->AddRow(); a->AddRow();
            auto cell = [&](int row) { return reinterpret_cast<CKParameterOut *>(*a->GetElement(row, 0)); };
            auto wire = [&](bool reverse, int value) {
                for (auto *p : inputs) { check(p->SetDirectSource(nullptr) == CK_OK, "clear fixture input"); p->Enable(TRUE); }
                int first = reverse ? 2 : 0, last = reverse ? 0 : 2;
                check(inputs[last]->SetDirectSource(cell(0)) == CK_OK, "fixture direct root");
                check(inputs[1]->ShareSourceWith(inputs[last]) == CK_OK, "fixture middle chain");
                check(inputs[first]->ShareSourceWith(inputs[1]) == CK_OK, "fixture reversed chain");
                check(inputs[3]->ShareSourceWith(inputs[first]) == CK_OK, "fixture fanout alias");
                check(inputs[4]->SetDirectSource(cell(1)) == CK_OK, "fixture second row");
                inputs[3]->Enable(FALSE);
                cell(0)->SetValue(&value); ++value; cell(1)->SetValue(&value);
            };
            auto verify = [&](bool reverse, int value) {
                int first = reverse ? 2 : 0, last = reverse ? 0 : 2;
                check(inputs[first]->GetSharedSource() == inputs[1], "saved first shared edge");
                check(inputs[1]->GetSharedSource() == inputs[last], "saved middle shared edge");
                check(inputs[last]->GetDirectSource() == cell(0), "saved semantic root cell");
                check(inputs[3]->GetSharedSource() == inputs[first], "saved fanout alias");
                check(inputs[4]->GetDirectSource() == cell(1), "saved semantic second row");
                for (int i = 0; i < 5; ++i) {
                    check((inputs[i]->IsEnabled() != FALSE) == (i != 3), "saved input enable flag");
                    auto *source = inputs[i]->GetRealSource();
                    check(source == cell(i == 4 ? 1 : 0), "resolved shared source after row rebuild");
                    int actual = -1; if (source == cell(i == 4 ? 1 : 0)) source->GetValue(&actual, FALSE);
                    check(actual == value + (i == 4 ? 1 : 0), "saved value through shared chain");
                }
            };
            for (int round = 0; round < 4; ++round) {
                bool reverse = round % 2; int value = 100 + round * 10;
                wire(reverse, value); check(capture() == 1, "capture graph / overwrite prior checkpoint");
                wire(!reverse, -100);
                for (int repeat = 0; repeat < 2; ++repeat) {
                    // Remove and recreate cells so IDs differ from the checkpoint.
                    a->RemoveRow(0); a->RemoveRow(0); a->AddRow(); a->AddRow();
                    check(restore() == 1, "restore graph after opposite chain and destroyed cells");
                    verify(reverse, value);
                }
            }
        }
        fprintf(log, "RESULT checks=%u failures=%u\n", checks, failures); fclose(log); log = nullptr;
        m_BML->ExitGame();
    }
};
IMod *BMLEntry(IBML *b) { return new SharedInputProbe(b); }
void BMLExit(IMod *m) { delete m; }
