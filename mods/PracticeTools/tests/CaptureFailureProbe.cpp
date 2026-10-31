#include <BML/BMLAll.h>
#include <dlfcn.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <array>
MOD_EXPORT IMod *BMLEntry(IBML *);
MOD_EXPORT void BMLExit(IMod *);
class CaptureFailureProbe final : public IMod {
public:
 explicit CaptureFailureProbe(IBML *b):IMod(b){}
 const char *GetID() override{return "CaptureFailureProbe";}
 const char *GetVersion() override{return "1.0";}
 const char *GetName() override{return "Failed overwrite checkpoint regression";}
 const char *GetAuthor() override{return "gzz2000";}
 const char *GetDescription() override{return "Test-only actual PracticeTools capture/restore entry points";}
 DECLARE_BML_VERSION;
 unsigned ticks=0,checks=0,failures=0;bool done=false;FILE *log=nullptr;
 void check(bool ok,const char *what){++checks;if(!ok)++failures;fprintf(log,"%s %s\n",ok?"PASS":"FAIL",what);fflush(log);}
 void OnProcess() override{
  if (!getenv("BALLANCE_CAPTURE_PROBE_LOG") || !getenv("BALLANCE_CAPTURE_PROBE_PRACTICE")) return;
  if(done||!m_BML->IsPlaying()||++ticks<4000)return;done=true;
  log=fopen(getenv("BALLANCE_CAPTURE_PROBE_LOG"),"w");if(!log)return;
  void *lib=dlopen(getenv("BALLANCE_CAPTURE_PROBE_PRACTICE"),RTLD_NOW|RTLD_LOCAL);
  auto capture=lib?reinterpret_cast<int(*)()>(dlsym(lib,"BML_PracticeCaptureState")):nullptr;
  auto restore=lib?reinterpret_cast<int(*)()>(dlsym(lib,"BML_PracticeRestoreState")):nullptr;
  check(capture&&restore,"resolve actual API");
  if(capture&&restore){
   auto *ctx=m_BML->GetCKContext();std::array<CKParameterOut*,3> p{};
   for(auto &x:p)x=ctx->CreateCKParameterOut("capture failure value",CKPGUID_INT,FALSE);
   auto *a=static_cast<CKDataArray*>(ctx->CreateObject(CKCID_DATAARRAY,"capture failure array"));
   a->InsertColumn(-1,CKARRAYTYPE_PARAMETER,"value",CKPGUID_INT);a->InsertColumn(-1,CKARRAYTYPE_STRING,"label");a->AddRow();a->AddRow();
   auto write=[&](int base){
    for(auto *x:p)x->RemoveAllDestinations();
    for(int i=0;i<3;++i){int v=base+i;p[i]->SetValue(&v);}
    p[2]->AddDestination(p[1]);p[2]->AddDestination(p[0]);
    for(int i=0;i<2;++i){auto *cell=reinterpret_cast<CKParameterOut*>(*a->GetElement(i,0));int v=base+10+i;cell->SetValue(&v);}
    a->SetElementStringValue(0,1,base%2?"odd":"even");
   };
   auto verify=[&](int base){
    for(int i=0;i<3;++i){int v=0;p[i]->GetValue(&v,FALSE);check(v==base+i,"independent committed parameter value");}
    check(p[2]->GetDestinationCount()==2&&p[2]->GetDestination(0)==p[1]&&p[2]->GetDestination(1)==p[0],"committed ordered edges");
    for(int i=0;i<2;++i){auto *cell=reinterpret_cast<CKParameterOut*>(*a->GetElement(i,0));int v=0;cell->GetValue(&v,FALSE);check(v==base+10+i,"committed typed array cell");}
    char text[16]{};a->GetElementStringValue(0,1,text,sizeof(text));check(strcmp(text,base%2?"odd":"even")==0,"committed array string");
   };
   write(10);setenv("BALLANCE_PRACTICE_CAPTURE_FAIL_STAGE","before-commit",1);
   check(capture()==0,"initial failed save rejects");unsetenv("BALLANCE_PRACTICE_CAPTURE_FAIL_STAGE");check(restore()==0,"initial failure does not publish incomplete checkpoint");verify(10);
   int base=101;
   for(const char *stage:{"parameter-value","array-value","after-values","after-physics","before-commit"}){
    fprintf(log,"STAGE %s\n",stage);write(base);check(capture()==1,"commit baseline before injected overwrite");
    write(base+100);setenv("BALLANCE_PRACTICE_CAPTURE_FAIL_STAGE",stage,1);check(capture()==0,"overwrite failure reported");unsetenv("BALLANCE_PRACTICE_CAPTURE_FAIL_STAGE");verify(base+100);
    for(int i=0;i<2;++i){check(restore()==1,"old checkpoint remains restorable after failed overwrite");verify(base);}
    write(base+1);check(capture()==1,"successful overwrite after failure");write(-99);check(restore()==1,"restore new committed snapshot");verify(base+1);base+=10;
   }
   for(int i=0;i<4;++i){write(501+i);check(capture()==1,"repeated successful overwrite warms/reuses scratch bank");write(-99);check(restore()==1,"restore after scratch bank reuse");verify(501+i);}
  }
  fprintf(log,"RESULT checks=%u failures=%u\n",checks,failures);fclose(log);log=nullptr;m_BML->ExitGame();
 }
};
IMod *BMLEntry(IBML *b){return new CaptureFailureProbe(b);}
void BMLExit(IMod *m){delete m;}
