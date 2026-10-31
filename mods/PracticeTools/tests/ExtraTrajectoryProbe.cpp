// In-game ordinary-mode fixture. Uses a real loaded level's TT Extra and
// public PracticeTools API, without TAS. A synthetic target and direct calls to
// the actual Extra function isolate its orbit/flyaway/absorption state machine;
// outer level triggers and real player motion are covered separately by tapes.
#include <BML/BMLAll.h>
#include <dlfcn.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <cstdint>
MOD_EXPORT IMod *BMLEntry(IBML *);
MOD_EXPORT void BMLExit(IMod *);
class ExtraTrajectoryProbe final : public IMod {
    struct Node {Node *next; CK3dEntity *ball; VxVector previous; float force; bool active;};
    using Action=int(*)();
    CKBehavior *extra=nullptr; CK3dEntity *ghost=nullptr; VxVector center{};
    Action capture=nullptr,restore=nullptr; FILE *log=nullptr;
    unsigned warmup=0,phase=0,frame=0,repeat=0,checks=0,failures=0;
    bool done=false,waiting=false;
    std::vector<std::vector<std::uint32_t>> reference;
    Node *list(){Node *p=nullptr;extra->GetLocalParameterValue(1,&p);return p;}
    void advance(){auto bc=m_BML->GetCKContext()->m_BehaviorContext;bc.Behavior=extra;bc.DeltaTime=m_BML->GetTimeManager()->GetLastDeltaTime();extra->GetFunction()(bc);}
    int value(int i){int v=0;extra->GetLocalParameterValue(i,&v);return v;}
    void check(bool ok,const char *why){++checks;if(!ok){++failures;fprintf(log,"FAIL phase=%u frame=%u repeat=%u %s\n",phase,frame,repeat,why);fflush(log);}}
    void finish(){done=true;fprintf(log,"RESULT checks=%u failures=%u phases=%u\n",checks,failures,phase);fclose(log);log=nullptr;m_BML->ExitGame();}
    std::vector<std::uint32_t> sample(){
        std::vector<std::uint32_t> words;
        auto raw=[&](const void *p,size_t n){auto *s=static_cast<const std::uint32_t*>(p);words.insert(words.end(),s,s+n/4);};
        for(int i:{0,4,5,6,7,8,9,10}){auto *p=extra->GetLocalParameter(i);raw(p->GetReadDataPtr(FALSE),p->GetDataSize());}
        auto entity=[&](CK3dEntity *b){
            words.push_back(b->IsVisible());VxVector p{};b->GetPosition(&p);raw(&p,sizeof(p));
            words.push_back(b->GetScriptCount());for(int j=0;j<b->GetScriptCount();++j)words.push_back(b->GetScript(j)->IsActive());
        };
        for(auto *p=list();p;p=p->next){words.push_back(p->active);raw(&p->previous,sizeof(p->previous));raw(&p->force,sizeof(p->force));entity(p->ball);
            words.push_back(p->ball->GetChildrenCount());for(int i=0;i<p->ball->GetChildrenCount();++i)entity(p->ball->GetChild(i));}
        auto *a=m_BML->GetArrayByName("Energy");int score=0;if(a)a->GetElementValue(0,0,&score);words.push_back(score);
        return words;
    }
    bool initialize(){
        auto *ctx=m_BML->GetCKContext();
        auto &objects=ctx->GetObjectListByType(CKCID_BEHAVIOR,TRUE);
        for(auto *o:objects){auto *b=static_cast<CKBehavior*>(o);if(b->GetPrototypeGuid()!=CKGUID(0x36106bd9,0x51813906))continue;
            auto *t=b->GetTarget();int status=-1;b->GetLocalParameterValue(4,&status);
            int active=0;b->GetLocalParameterValue(0,&active);if(warmup==4000&&t) {fprintf(stderr,"EXTRA_CANDIDATE %s status=%d active=%d activation=%d\n",t->GetName(),status,b->IsActive(),active);}
            if(t&&t->GetName()&&!strncmp(t->GetName(),"P_Extra_Point_MF",16)&&status==0){extra=b;break;}}
        if(!extra)return false;
        log=fopen(getenv("BALLANCE_EXTRA_PROBE_LOG"),"w");if(!log){done=true;return false;}
        auto *library=dlopen(getenv("BALLANCE_EXTRA_PROBE_PRACTICE"),RTLD_NOW|RTLD_LOCAL);
        capture=library?reinterpret_cast<Action>(dlsym(library,"BML_PracticeCaptureState")):nullptr;
        restore=library?reinterpret_cast<Action>(dlsym(library,"BML_PracticeRestoreState")):nullptr;
        check(capture&&restore,"API available");if(!capture||!restore){finish();return false;}
        ghost=static_cast<CK3dEntity*>(ctx->CreateObject(CKCID_3DENTITY,"Extra trajectory target"));
        auto *source=extra->CreateLocalParameter("Extra probe target",CKPGUID_3DENTITY);
        CK_ID id=ghost->GetID();source->SetValue(&id);extra->GetInputParameter(0)->SetDirectSource(source);
        static_cast<CK3dEntity*>(extra->GetTarget())->GetPosition(&center);
        VxVector far=center;far.x+=100;ghost->SetPosition(&far);
        fprintf(log,"START target=%s nodes=%d active=%d playing=%d\n",extra->GetTarget()->GetName(),value(7),extra->IsActive(),m_BML->IsPlaying());
        extra->ActivateInput(0);advance();check(capture()==1,"idle capture");frame=0;return true;
    }
public:
    explicit ExtraTrajectoryProbe(IBML *b):IMod(b){}
    const char *GetID()override{return "ExtraTrajectoryProbe";}
    const char *GetVersion()override{return "1";}
    const char *GetName()override{return "Ordinary collectible rewind regression";}
    const char *GetAuthor()override{return "gzz2000";}
    const char *GetDescription()override{return "Test-only real Extra trajectories through repeated absorb/restore cycles";}
    DECLARE_BML_VERSION;
    void OnPostStartMenu() override {
        if (!getenv("BALLANCE_EXTRA_PROBE_LOG")) return;
        m_BML->AddTimer(CKDWORD(5), [this]() {
            auto *ctx=m_BML->GetCKContext();
            auto *array=m_BML->GetArrayByName("CurrentLevel");int level=getenv("BALLANCE_EXTRA_PROBE_LEVEL")?atoi(getenv("BALLANCE_EXTRA_PROBE_LEVEL")):2;array->SetElementValue(0,0,&level);
            auto *messages=ctx->GetMessageManager();
            messages->SendMessageSingle(messages->AddMessageType((CKSTRING)"Load Level"),ctx->GetCurrentLevel());
            messages->SendMessageSingle(messages->AddMessageType((CKSTRING)"Menu_Load"),m_BML->GetGroupByName("All_Sound"));
            if(auto *black=m_BML->Get2dEntityByName("M_BlackScreen"))black->Show(CKHIDE);
            auto *exit=ScriptHelper::FindFirstBB(m_BML->GetScriptByName("Menu_Main"),"Exit",false,1,0);
            exit->ActivateInput(0);exit->Activate();
        });
    }
    void OnProcess()override{
        if(done||!getenv("BALLANCE_EXTRA_PROBE_LOG")||!getenv("BALLANCE_EXTRA_PROBE_PRACTICE")||!m_BML->IsPlaying())return;
        if(!extra){if(++warmup<4000)return;initialize();return;}
        if(waiting){
            // Overwrite the checkpoint in successive live phases, after testing
            // the preceding checkpoint twice from the fully consumed future.
            const bool ready=phase==1 ? value(4)==1 : value(4)==2&&value(7)>0&&value(7)<6;
            ghost->SetPosition(&center);
            if(!ready){advance();if(++frame>2500){check(false,"reach next capture phase");finish();}return;}
            check(capture()==1,"overwrite capture");waiting=false;frame=0;reference.clear();repeat=0;return;
        }
        auto state=sample();
        if(!repeat)reference.push_back(state);
        else {check(frame<reference.size(),"replay length");if(frame<reference.size()){if(state!=reference[frame]){for(size_t w=0;w<state.size()&&w<reference[frame].size();++w)if(state[w]!=reference[frame][w])fprintf(log,"WORD %zu/%zu actual=%08x expected=%08x\n",w,state.size(),state[w],reference[frame][w]);}check(state==reference[frame],"exact trajectory/private state/visibility/scripts/score");}}
        ghost->SetPosition(&center);
        if(value(7)==0){
            fprintf(log,"ABSORBED phase=%u repeat=%u frame=%u rows=%zu failures=%u\n",phase,repeat,frame,reference.size(),failures);fflush(log);
            if(repeat)check(frame+1==reference.size(),"all satellites recaptured at same tick");
            if(repeat<2){check(restore()==1,"restore consumed future");++repeat;frame=0;return;}
            if(++phase==3){finish();return;}
            check(restore()==1,"rewind for overwrite");waiting=true;frame=0;return;
        }
        advance();
        if(++frame>2500){check(false,"satellites eventually absorbed");finish();}
    }
};
IMod *BMLEntry(IBML *b){return new ExtraTrajectoryProbe(b);}
void BMLExit(IMod *m){delete m;}
