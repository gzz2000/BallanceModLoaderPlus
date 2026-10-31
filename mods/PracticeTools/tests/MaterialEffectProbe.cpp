#include <BML/BMLAll.h>
#include <dlfcn.h>
#include <cstdio>
#include <cstdlib>

MOD_EXPORT IMod *BMLEntry(IBML *);
MOD_EXPORT void BMLExit(IMod *);

// Isolated in-game fixture: exercises the same API as F5/F8. No renderer mock.
class MaterialEffectProbe final : public IMod {
public:
    explicit MaterialEffectProbe(IBML *b) : IMod(b) {}
    const char *GetID() override { return "MaterialEffectProbe"; }
    const char *GetVersion() override { return "1.0"; }
    const char *GetName() override { return "Material effect checkpoint regression"; }
    const char *GetAuthor() override { return "gzz2000"; }
    const char *GetDescription() override { return "Test-only material parameter lifecycle fixture"; }
    DECLARE_BML_VERSION;
    unsigned ticks = 0, checks = 0, failures = 0;
    bool done = false;
    FILE *log = nullptr;
    void check(bool ok, const char *what) {
        ++checks;
        if (!ok) ++failures;
        fprintf(log, "%s %s\n", ok ? "PASS" : "FAIL", what);
        fflush(log);
    }
    void OnProcess() override {
        if (!getenv("BALLANCE_CAPTURE_PROBE_LOG") ||
            !getenv("BALLANCE_CAPTURE_PROBE_PRACTICE") || done ||
            !m_BML->IsPlaying() || ++ticks < 4000) return;
        done = true;
        log = fopen(getenv("BALLANCE_CAPTURE_PROBE_LOG"), "w");
        if (!log) return;
        void *lib = dlopen(getenv("BALLANCE_CAPTURE_PROBE_PRACTICE"), RTLD_NOW | RTLD_LOCAL);
        auto save = lib ? reinterpret_cast<int (*)()>(dlsym(lib, "BML_PracticeCaptureState")) : nullptr;
        auto restore = lib ? reinterpret_cast<int (*)()>(dlsym(lib, "BML_PracticeRestoreState")) : nullptr;
        check(save && restore, "actual snapshot API");
        if (save && restore) {
            auto *ctx = m_BML->GetCKContext();
            auto *material = CKMaterial::Cast(ctx->CreateObject(CKCID_MATERIAL, "effect checkpoint fixture"));
            auto *target = CK3dEntity::Cast(ctx->CreateObject(CKCID_3DENTITY, "effect reference target"));
            auto *reference = ctx->CreateCKParameterOut("effect child reference", CKPGUID_OBJECT);
            auto member = [&](CKParameter *p, int i) -> CKParameter * {
                if (!p || p->GetGUID() != CKPGUID_TEXGENREFEFFECT ||
                    p->GetDataSize() < 2 * int(sizeof(CK_ID))) return nullptr;
                auto *ids = static_cast<const CK_ID *>(p->GetReadDataPtr(FALSE));
                return ids ? CKParameter::Cast(ctx->GetObject(ids[i])) : nullptr;
            };
            const VX_EFFECT effects[] = {VXEFFECT_TEXGEN, VXEFFECT_TEXGENREF,
                VXEFFECT_NONE, VXEFFECT_TEXGENREF, VXEFFECT_TEXGEN};
            bool stop = false;
            for (int round = 0; round < 5 && !stop; ++round) {
                const auto effect = effects[round];
                material->SetEffect(effect);
                auto *root = material->GetEffectParameter();
                CKDWORD expected = VXEFFECT_TGNONE;
                CK_ID expectedTarget = target->GetID();
                CK_ID savedReference = 0;
                if (effect == VXEFFECT_TEXGENREF) {
                    auto *mode = member(root, 0);
                    auto *ref = member(root, 1);
                    check(mode && ref, "structured effect owns two members");
                    if (!mode || !ref) break;
                    mode->SetValue(&expected);
                    ref->SetValue(&expectedTarget);
                    savedReference = mode->GetID();
                } else if (effect == VXEFFECT_TEXGEN) {
                    check(root != nullptr, "enum effect owns parameter");
                    if (!root) break;
                    root->SetValue(&expected);
                    savedReference = root->GetID();
                }
                reference->SetValue(&savedReference);
                check(save() == 1, "capture / overwrite material effect");
                for (int repeat = 0; repeat < 3; ++repeat) {
                    // Alternate deletion, type replacement, and same-type mutation.
                    if (repeat == 0) material->SetEffect(effect == VXEFFECT_NONE ? VXEFFECT_TEXGENREF : VXEFFECT_NONE);
                    else if (repeat == 1) material->SetEffect(effect == VXEFFECT_TEXGENREF ? VXEFFECT_TEXGEN : VXEFFECT_TEXGENREF);
                    else if (auto *p = material->GetEffectParameter()) {
                        CKDWORD changed = VXEFFECT_TGREFLECT;
                        if (effect == VXEFFECT_TEXGENREF) member(p, 0)->SetValue(&changed);
                        else p->SetValue(&changed);
                    }
                    bool ok = restore() == 1;
                    check(ok, "restore after effect deletion/type/value change");
                    if (!ok) { stop = true; break; }
                    check(material->GetEffect() == effect, "saved effect selected");
                    root = material->GetEffectParameter();
                    CK_ID actualReference = ~CK_ID(0);
                    reference->GetValue(&actualReference, FALSE);
                    if (effect == VXEFFECT_NONE) {
                        check(root == nullptr, "NONE removes abandoned parameter");
                        check(actualReference == 0, "saved null reference");
                        continue;
                    }
                    auto *mode = effect == VXEFFECT_TEXGENREF ? member(root, 0) : root;
                    check(mode != nullptr, "restored parameter type");
                    if (!mode) { stop = true; break; }
                    CKDWORD actual = ~CKDWORD(0);
                    mode->GetValue(&actual, FALSE);
                    check(actual == expected, "saved nondefault effect value");
                    check(actualReference == mode->GetID(), "external reference rebound to semantic parameter");
                    if (effect == VXEFFECT_TEXGENREF) {
                        auto *ref = member(root, 1);
                        CK_ID id = 0;
                        check(ref && ref->GetValue(&id, FALSE) == CK_OK && id == expectedTarget,
                            "structured effect entity reference restored");
                    }
                }
            }
        }
        fprintf(log, "RESULT checks=%u failures=%u\n", checks, failures);
        fclose(log); log = nullptr;
        m_BML->ExitGame();
    }
};
IMod *BMLEntry(IBML *b) { return new MaterialEffectProbe(b); }
void BMLExit(IMod *m) { delete m; }
