#include <BML/BMLAll.h>
#include <dlfcn.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <array>
#include <vector>
#include <cmath>

MOD_EXPORT IMod *BMLEntry(IBML *);
MOD_EXPORT void BMLExit(IMod *);

class AnimationStateProbe final : public IMod {
public:
    explicit AnimationStateProbe(IBML *b) : IMod(b) {}
    const char *GetID() override { return "AnimationStateProbe"; }
    const char *GetVersion() override { return "1.0"; }
    const char *GetName() override { return "Animation checkpoint state regression"; }
    const char *GetAuthor() override { return "gzz2000"; }
    const char *GetDescription() override { return "Test-only animation ownership, timing and key data fixture"; }
    DECLARE_BML_VERSION;
    unsigned ticks=0,checks=0,failures=0;
    bool done=false;
    FILE *log=nullptr;
    void check(bool ok,const char *what) {
        ++checks;if (!ok) ++failures;
        fprintf(log,"%s %s\n",ok?"PASS":"FAIL",what);fflush(log);
    }
    void OnProcess() override {
        if (!getenv("BALLANCE_CAPTURE_PROBE_LOG") ||
            !getenv("BALLANCE_CAPTURE_PROBE_PRACTICE") || done ||
            !m_BML->IsPlaying() || ++ticks<4000) return;
        done=true;log=fopen(getenv("BALLANCE_CAPTURE_PROBE_LOG"),"w");if (!log) return;
        void *library=dlopen(getenv("BALLANCE_CAPTURE_PROBE_PRACTICE"),RTLD_NOW|RTLD_LOCAL);
        auto save=library?reinterpret_cast<int(*)()>(dlsym(library,"BML_PracticeCaptureState")):nullptr;
        auto restore=library?reinterpret_cast<int(*)()>(dlsym(library,"BML_PracticeRestoreState")):nullptr;
        check(save&&restore,"actual snapshot API");
        if (save&&restore) { run(save,restore); if (!failures) controllers(save,restore); if (!failures) lifecycle(); }
        fprintf(log,"RESULT checks=%u failures=%u\n",checks,failures);
        fclose(log);log=nullptr;m_BML->ExitGame();
    }


    void lifecycle() {
        const char *path=getenv("BALLANCE_ANIMATION_PROBE_RENDERER");
        void *lib=path?dlopen(path,RTLD_NOW|RTLD_LOCAL):nullptr;
        using Capture=int(*)(CKContext *,void **);
        using Prepare=int(*)(CKContext *,void *,void **);
        using Commit=int(*)(CKContext *,void *);
        using Destroy=void(*)(void *);
        auto capture=lib?reinterpret_cast<Capture>(dlsym(lib,"CKCaptureAnimationState")):nullptr;
        auto prepare=lib?reinterpret_cast<Prepare>(dlsym(lib,"CKPrepareAnimationRestore")):nullptr;
        auto commit=lib?reinterpret_cast<Commit>(dlsym(lib,"CKCommitAnimationRestore")):nullptr;
        auto destroy=lib?reinterpret_cast<Destroy>(dlsym(lib,"CKDestroyAnimationState")):nullptr;
        auto destroyPrepared=lib?reinterpret_cast<Destroy>(dlsym(lib,"CKDestroyAnimationRestore")):nullptr;
        check(capture&&prepare&&commit&&destroy&&destroyPrepared,"animation provider API");
        if (!capture||!prepare||!commit||!destroy||!destroyPrepared) return;
        auto *ctx=m_BML->GetCKContext();
        auto *a=CKObjectAnimation::Cast(ctx->CreateObject(CKCID_OBJECTANIMATION,"lifetime animation"));
        check(a!=nullptr,"lifetime fixture owner");if (!a)return;
        a->SetLength(123);void *saved=nullptr,*prepared=nullptr;
        check(capture(ctx,&saved)==CK_OK,"provider capture");if (!saved)return;
        check(prepare(ctx,saved,&prepared)==CK_OK,"provider prepares without mutation");if (!prepared){destroy(saved);return;}
        auto *later=ctx->CreateObject(CKCID_3DENTITY,"unreconciled animation entity");
        a->SetLength(456);
        check(later && commit(ctx,prepared)==CKERR_INVALIDOBJECT,"unreconciled timeline rejected before commit");
        check(a->GetLength()==456,"rejected commit leaves current state intact");
        if (later) ctx->DestroyObject(later);
        check(commit(ctx,prepared)==CK_OK && a->GetLength()==123,"same preparation commits after lifetime reconciliation");
        a->SetLength(789);
        check(commit(ctx,prepared)==CKERR_INVALIDPARAMETER && a->GetLength()==789,"prepared restore is consumed once");
        destroyPrepared(prepared);prepared=nullptr;
        check(prepare(ctx,saved,&prepared)==CK_OK,"checkpoint reusable after commit");
        ctx->DestroyObject(a);
        check(commit(ctx,prepared)==CKERR_INVALIDOBJECT,"destroyed owner rejects prepared commit");
        destroyPrepared(prepared);prepared=reinterpret_cast<void *>(1);
        check(prepare(ctx,saved,&prepared)==CKERR_INVALIDOBJECT && prepared==nullptr,"destroyed owner rejects preparation");
        destroy(saved);
        check(commit(ctx,reinterpret_cast<void *>(1))==CKERR_INVALIDPARAMETER,"invalid prepared handle rejected");
        dlclose(lib);
    }
    struct ControllerCase { CKObjectAnimation *animation; CKANIMATION_CONTROLLER type; };
    CKAnimController *get(const ControllerCase &c) {
        switch (c.type & CKANIMATION_CONTROLLER_MASK) {
        case CKANIMATION_CONTROLLER_POS:return c.animation->GetPositionController();
        case CKANIMATION_CONTROLLER_ROT:return c.animation->GetRotationController();
        case CKANIMATION_CONTROLLER_SCL:return c.animation->GetScaleController();
        case CKANIMATION_CONTROLLER_SCLAXIS:return c.animation->GetScaleAxisController();
        default:return c.animation->GetMorphController();
        }
    }
    std::array<unsigned char,64> evaluate(CKAnimController *c) {
        std::array<unsigned char,64> out{};
        CKBOOL evaluated=FALSE;
        if (c->GetType()==CKANIMATION_MORPH_CONTROL) {
            VxVector positions[2]{};
            VxCompressedVector normals[2]{};
            auto *m=static_cast<CKMorphController *>(c);
            evaluated=m->Evaluate(37,2,positions,sizeof(VxVector),normals);
            std::memcpy(out.data(),positions,sizeof(positions));
            std::memcpy(out.data()+32,normals,sizeof(normals));
        } else if ((c->GetType() & CKANIMATION_CONTROLLER_MASK)==CKANIMATION_CONTROLLER_ROT ||
                   (c->GetType() & CKANIMATION_CONTROLLER_MASK)==CKANIMATION_CONTROLLER_SCLAXIS) {
            VxQuaternion value(0,0,0,0);
            evaluated=c->Evaluate(37,&value);
            std::memcpy(out.data(),&value,sizeof(value));
        } else {
            VxVector value(0,0,0);
            evaluated=c->Evaluate(37,&value);
            std::memcpy(out.data(),&value,sizeof(value));
        }
        check(evaluated!=FALSE,"controller fixture evaluation succeeds");
        return out;
    }
    std::vector<unsigned char> wire(CKAnimController *c) {
        int n=c->DumpKeysTo(nullptr);
        if (n<=0) return {};
        std::vector<unsigned char> out(n);c->DumpKeysTo(out.data());return out;
    }
    void controllers(int(*save)(),int(*restore)()) {
        auto *ctx=m_BML->GetCKContext();
        CKANIMATION_CONTROLLER types[]={CKANIMATION_LINPOS_CONTROL,CKANIMATION_LINROT_CONTROL,
            CKANIMATION_LINSCL_CONTROL,CKANIMATION_LINSCLAXIS_CONTROL,CKANIMATION_TCBPOS_CONTROL,
            CKANIMATION_TCBROT_CONTROL,CKANIMATION_TCBSCL_CONTROL,CKANIMATION_TCBSCLAXIS_CONTROL,
            CKANIMATION_BEZIERPOS_CONTROL,CKANIMATION_BEZIERSCL_CONTROL,CKANIMATION_MORPH_CONTROL};
        std::vector<ControllerCase> cases;
        for (auto type:types) {
            auto *a=CKObjectAnimation::Cast(ctx->CreateObject(CKCID_OBJECTANIMATION,"controller snapshot fixture"));
            CKAnimController *c=a?a->CreateController(type):nullptr;
            check(c!=nullptr,"create controller kind");if (!c) return;
            if (type==CKANIMATION_MORPH_CONTROL) static_cast<CKMorphController *>(c)->SetMorphVertexCount(2);
            for (int i=0;i<3;++i) {
                VxVector v(float(i*3),float(i*i),float(i+1));
                VxQuaternion q(0,std::sin(float(i)*.3f),0,std::cos(float(i)*.3f));
                CKPositionKey position(float(i*50),v);
                CKRotationKey rotation(float(i*50),q);
                CKTCBPositionKey tcbPosition(float(i*50),v,.2f,-.1f,.3f,.1f,.2f);
                CKTCBRotationKey tcbRotation(float(i*50),q,.2f,-.1f,.3f,.1f,.2f);
                CKBezierPositionKey bezier;bezier.TimeStep=float(i*50);bezier.Pos=v;
                switch (type) {
                case CKANIMATION_TCBPOS_CONTROL:case CKANIMATION_TCBSCL_CONTROL:c->AddKey(&tcbPosition);break;
                case CKANIMATION_TCBROT_CONTROL:case CKANIMATION_TCBSCLAXIS_CONTROL:c->AddKey(&tcbRotation);break;
                case CKANIMATION_BEZIERPOS_CONTROL:case CKANIMATION_BEZIERSCL_CONTROL:c->AddKey(&bezier);break;
                case CKANIMATION_LINROT_CONTROL:case CKANIMATION_LINSCLAXIS_CONTROL:c->AddKey(&rotation);break;
                case CKANIMATION_MORPH_CONTROL: {
                    auto *m=static_cast<CKMorphController *>(c);int idx=m->AddKey(float(i*50),i!=1);
                    auto *key=static_cast<CKMorphKey *>(m->GetKey(idx));
                    if (!key || !key->PosArray) {check(false,"allocate morph key");return;}
                    key->PosArray[0]=v;key->PosArray[1]=VxVector(v.z,v.x,v.y);
                    if (key->NormArray) std::memset(key->NormArray,0,2*sizeof(VxCompressedVector));
                    break;
                }
                default:c->AddKey(&position);break;
                }
            }
            a->SetLength(100);c->SetLength(125);cases.push_back({a,type});
            check(!wire(c).empty(),"controller key data valid");
        }
        auto *owner=cases[0].animation;
        auto *share1=CKObjectAnimation::Cast(ctx->CreateObject(CKCID_OBJECTANIMATION,"keydata sharer 1"));
        auto *share2=CKObjectAnimation::Cast(ctx->CreateObject(CKCID_OBJECTANIMATION,"keydata sharer 2"));
        auto *merged=owner->CreateMergedAnimation(cases[2].animation);
        check(share1&&share2&&merged,"shared and merged graph objects");if (!share1||!share2||!merged) return;
        share1->ShareDataFrom(owner);share2->ShareDataFrom(owner);merged->SetMergeFactor(.3f);
        // Include ordered reciprocal entity lists and allocated-but-empty lists.
        auto *target=CK3dEntity::Cast(ctx->CreateObject(CKCID_3DENTITY,"ordered animation owner"));
        auto *other=CK3dEntity::Cast(ctx->CreateObject(CKCID_3DENTITY,"empty animation list"));
        owner->Set3dEntity(target);share1->Set3dEntity(target);share2->Set3dEntity(other);share2->Set3dEntity(target);
        const CKDWORD mergedFlags=merged->GetFlags();
        for (int overwrite=0;overwrite<3;++overwrite) {
            // Warm caches, then directly edit a key. A snapshot must retain
            // this exact lazy-cache state rather than normalize/recalculate it.
            std::vector<std::vector<unsigned char>> keys;
            std::vector<std::array<unsigned char,64>> values;
            for (auto &item:cases) {
                auto *c=get(item);evaluate(c);
                auto *key=c->GetKey(1);
                const auto kind=item.type & CKANIMATION_CONTROLLER_MASK;
                if (kind==CKANIMATION_CONTROLLER_POS || kind==CKANIMATION_CONTROLLER_SCL)
                    static_cast<CKPositionKey *>(key)->Pos.x+=float(overwrite+1);
                keys.push_back(wire(c));values.push_back(evaluate(c));
            }
            VxVector mergedValue;merged->EvaluatePosition(37,mergedValue);
            check(save()==1,"capture controller graph overwrite");
            for (int repeat=0;repeat<3;++repeat) {
                for (auto &item:cases) {
                    auto *c=get(item);c->RemoveKey(1);c->SetLength(999);
                    item.animation->SetFlags(item.animation->GetFlags()^0x20);
                }
                // Owner leaves the old alias group; exercise reference-count
                // ownership transfer, then break all three sharing relations.
                owner->ShareDataFrom(nullptr);share1->ShareDataFrom(nullptr);share2->ShareDataFrom(nullptr);
                share1->Set3dEntity(other);owner->Set3dEntity(other);
                merged->SetFlags(0);merged->SetMergeFactor(.9f);
                const bool ok=restore()==1;check(ok,"restore full controller graph");if (!ok)return;
                for (size_t i=0;i<cases.size();++i) {
                    auto *c=get(cases[i]);check(c!=nullptr,"restored controller exists");if (!c)return;
                    check(c->GetType()==cases[i].type && c->GetLength()==125,"controller kind and individual length");
                    check(wire(c)==keys[i],"exact controller keys including morph payloads");
                    auto actual=evaluate(c);
                    check(std::memcmp(actual.data(),values[i].data(),sizeof(actual))==0,"next controller evaluation including lazy caches");
                }
                check(owner->Shared()==owner && share1->Shared()==owner && share2->Shared()==owner,"saved shared data owner");
                check(share1->GetPositionController()==owner->GetPositionController() && share2->GetPositionController()==owner->GetPositionController(),"saved controller alias group");
                check(target->GetObjectAnimationCount()==3 && target->GetObjectAnimation(0)==owner && target->GetObjectAnimation(1)==share1 && target->GetObjectAnimation(2)==share2 && other->GetObjectAnimationCount()==0,"saved reciprocal list order");
                VxVector value;merged->EvaluatePosition(37,value);
                check(merged->GetFlags()==mergedFlags && merged->GetMergeFactor()==.3f && std::memcmp(&value,&mergedValue,sizeof(value))==0,"saved merged-source evaluation");
            }
        }
    }
    void run(int(*save)(),int(*restore)()) {
        auto *ctx=m_BML->GetCKContext();
        auto *a=CK3dEntity::Cast(ctx->CreateObject(CKCID_3DENTITY,"animation target A"));
        auto *b=CK3dEntity::Cast(ctx->CreateObject(CKCID_3DENTITY,"animation target B"));
        auto *channel=CKObjectAnimation::Cast(ctx->CreateObject(CKCID_OBJECTANIMATION,"animation channel"));
        auto *other=CKObjectAnimation::Cast(ctx->CreateObject(CKCID_OBJECTANIMATION,"alternate channel"));
        auto *master=CKKeyedAnimation::Cast(ctx->CreateObject(CKCID_KEYEDANIMATION,"animation master"));
        check(a&&b&&channel&&other&&master,"animation fixture objects");
        if (!a||!b||!channel||!other||!master) return;
        VxVector first(0,0,0),last(20,0,0),future(80,0,0);
        channel->AddPositionKey(0,&first);channel->AddPositionKey(100,&last);
        channel->SetLength(100);channel->Set3dEntity(a);
        other->Set3dEntity(b);master->AddAnimation(channel);
        master->SetLength(100);master->LinkToFrameRate(TRUE,30);master->SetMergeFactor(.25f);
        check(master->GetRootEntity()==a,"derived animation root");
        master->CenterAnimation(25);channel->SetStep(.5f);master->SetCurrentStep(.5f);
        const auto savedFlags=master->GetFlags();
        const float savedNextFrame=master->GetNextFrame(16);
        VxVector savedEvaluation;
        check(channel->EvaluatePosition(50,savedEvaluation)!=FALSE,"saved controller evaluation");
        check(save()==1,"capture animation graph");
        for (int repeat=0;repeat<2;++repeat) {
            master->CenterAnimation(75);
            channel->AddPositionKey(100,&future);channel->SetLength(200);
            channel->Set3dEntity(b);
            master->Clear();master->AddAnimation(other);
            master->SetLength(240);master->LinkToFrameRate(FALSE,120);
            master->SetMergeFactor(.75f);master->SetCanBeInterrupt(FALSE);
            bool ok=restore()==1;check(ok,"restore accepts surviving animation owners");
            if (!ok) return;
            check(channel->Get3dEntity()==a,"saved animated entity binding");
            check(master->GetAnimationCount()==1 && master->GetAnimation(0)==channel,"saved ordered channel membership");
            check(channel->GetLength()==100,"saved channel length");
            check(master->GetLength()==100,"saved master length");
            check(master->GetFlags()==savedFlags,"saved animation control flags");
            check(master->IsLinkedToFrameRate() && master->GetLinkedFrameRate()==30,"saved frame rate policy");
            check(master->GetNextFrame(16)==savedNextFrame,"next animation frame uses saved time policy");
            check(master->GetMergeFactor()==.25f && channel->GetMergeFactor()==.25f,"saved master and channel blend factors");
            VxVector evaluated;
            check(channel->EvaluatePosition(50,evaluated) && std::memcmp(&evaluated,&savedEvaluation,sizeof(evaluated))==0,"saved key data evaluation");
            // A later tick must use the captured center, not only the pose that
            // PracticeTools writes back immediately after RestoreState.
            channel->SetStep(.25f);
            VxVector position;a->GetPosition(&position);
            check(position.x==0 && position.y==0 && position.z==0,"next pose uses saved root centering");
        }
    }
};
IMod *BMLEntry(IBML *b) { return new AnimationStateProbe(b); }
void BMLExit(IMod *m) { delete m; }
