#include <BML/BMLAll.h>
#include <dlfcn.h>
#include <cstdio>
#include <cstdlib>
MOD_EXPORT IMod *BMLEntry(IBML *);
MOD_EXPORT void BMLExit(IMod *);
class ReferenceProbe final : public IMod {
public:
 explicit ReferenceProbe(IBML *b):IMod(b){}
 const char *GetID() override{return "ReferenceProbe";}
 const char *GetVersion() override{return "1.0";}
 const char *GetName() override{return "Typed reference rewind regression";}
 const char *GetAuthor() override{return "gzz2000";}
 const char *GetDescription() override{return "Test-only actual snapshot API reference fixture";}
 DECLARE_BML_VERSION;
 unsigned ticks=0,checks=0,failures=0;bool done=false;FILE *log=nullptr;
 void check(bool ok,const char *s){++checks;if(!ok)++failures;fprintf(log,"%s %s\n",ok?"PASS":"FAIL",s);fflush(log);}
 void OnProcess() override {
  if(!getenv("BALLANCE_CAPTURE_PROBE_LOG")||!getenv("BALLANCE_CAPTURE_PROBE_PRACTICE")||done||!m_BML->IsPlaying()||++ticks<4000)return;
  done=true;log=fopen(getenv("BALLANCE_CAPTURE_PROBE_LOG"),"w");if(!log)return;
  void *lib=dlopen(getenv("BALLANCE_CAPTURE_PROBE_PRACTICE"),RTLD_NOW|RTLD_LOCAL);
  auto save=lib?reinterpret_cast<int(*)()>(dlsym(lib,"BML_PracticeCaptureState")):nullptr;
  auto restore=lib?reinterpret_cast<int(*)()>(dlsym(lib,"BML_PracticeRestoreState")):nullptr;
  check(save&&restore,"actual snapshot API");
  if(save&&restore){
   auto *ctx=m_BML->GetCKContext();
   auto *a=CKDataArray::Cast(ctx->CreateObject(CKCID_DATAARRAY,"reference targets"));
   a->InsertColumn(-1,CKARRAYTYPE_PARAMETER,"value",CKPGUID_INT);a->AddRow();
   auto cell=[&](){return reinterpret_cast<CKParameterOut *>(*a->GetElement(0,0));};
   auto *owner=CKBeObject::Cast(ctx->CreateObject(CKCID_BEOBJECT,"reference attribute owner"));
   auto type=ctx->GetAttributeManager()->RegisterNewAttributeType("reference fixture attribute",CKPGUID_INT);
   owner->SetAttribute(type);
   auto *obj=ctx->CreateCKParameterOut("object reference",CKPGUID_OBJECT);
   auto *list=ctx->CreateCKParameterOut("collection reference",CKPGUID_OBJECTARRAY);
   const CKGUID guid(0x73b75911,0x5a040001);XArray<CKGUID> types;types.PushBack(CKPGUID_OBJECT);types.PushBack(CKPGUID_OBJECTARRAY);
   check(ctx->GetParameterManager()->RegisterNewStructure(guid,"reference fixture structure","Object,Collection",types)==CK_OK,"register structure");
   auto *nested=ctx->CreateCKParameterOut("structured references",guid);
   a->InsertColumn(-1,CKARRAYTYPE_PARAMETER,"nested owned references",guid);
   auto structuredCell=[&](){return reinterpret_cast<CKParameter *>(*a->GetElement(0,1));};
   auto member=[&](CKParameter *p,int i){return CKParameter::Cast(ctx->GetObject(static_cast<const CK_ID *>(p->GetReadDataPtr(FALSE))[i]));};
   auto structType=ctx->GetAttributeManager()->RegisterNewAttributeType("structured fixture attribute",guid);
   owner->SetAttribute(structType);
   auto *arrayChildRef=ctx->CreateCKParameterOut("array child reference",CKPGUID_OBJECT);
   auto *attrChildRef=ctx->CreateCKParameterOut("attribute child reference",CKPGUID_OBJECT);
   auto *objectCells=CKDataArray::Cast(ctx->CreateObject(CKCID_DATAARRAY,"raw object cells"));
   objectCells->InsertColumn(-1,CKARRAYTYPE_OBJECT,"target");objectCells->AddRow();

   auto child=[&](int i){return CKParameter::Cast(ctx->GetObject(static_cast<const CK_ID *>(nested->GetReadDataPtr())[i]));};
   auto set=[&](CKParameter *p,CK_ID id){p->SetValue(&id);};
   auto setList=[&](CKParameter *p,CK_ID x,CK_ID y){XObjectArray *v=nullptr;p->GetValue(&v,FALSE);v->Clear();v->PushBack(x);v->PushBack(0);v->PushBack(y);v->PushBack(x);};
   auto checkObject=[&](CKParameter *p,CK_ID id){CK_ID actual=0;p->GetValue(&actual,FALSE);check(actual==id,"saved semantic object target");};
   auto checkList=[&](CKParameter *p,CK_ID x,CK_ID y){XObjectArray *v=nullptr;p->GetValue(&v,FALSE);check(v&&v->Size()==4,"saved collection size");if(v&&v->Size()==4){check((*v)[0]==x&&(*v)[1]==0&&(*v)[2]==y&&(*v)[3]==x,"saved collection order null duplicate and semantic targets");}};
   for(int round=0;round<4;++round){
    auto target=[&](){return round%2?owner->GetAttributeParameter(type)->GetID():cell()->GetID();};
    set(obj,target());set(child(0),target());setList(list,cell()->GetID(),owner->GetAttributeParameter(type)->GetID());setList(child(1),cell()->GetID(),owner->GetAttributeParameter(type)->GetID());
    set(member(structuredCell(),0),target());setList(member(structuredCell(),1),cell()->GetID(),owner->GetAttributeParameter(type)->GetID());
    set(member(owner->GetAttributeParameter(structType),0),target());setList(member(owner->GetAttributeParameter(structType),1),cell()->GetID(),owner->GetAttributeParameter(type)->GetID());
    set(arrayChildRef,member(structuredCell(),0)->GetID());set(attrChildRef,member(owner->GetAttributeParameter(structType),1)->GetID());
    CK_ID objectCell=member(structuredCell(),1)->GetID();objectCells->SetElementValue(0,0,&objectCell);
    check(save()==1,"capture / overwrite typed references");
    for(int repeat=0;repeat<2;++repeat){
     a->RemoveRow(0);a->AddRow();owner->RemoveAttribute(type);owner->SetAttribute(type);owner->RemoveAttribute(structType);owner->SetAttribute(structType);
     bool restored=restore()==1; check(restored,"restore after referenced cells and attribute destroyed");
     if(!restored || !owner->GetAttributeParameter(type)){ round=4; break; }
     checkObject(arrayChildRef,member(structuredCell(),0)->GetID());checkObject(attrChildRef,member(owner->GetAttributeParameter(structType),1)->GetID());
     checkObject(member(structuredCell(),0),target());checkObject(member(owner->GetAttributeParameter(structType),0),target());
     checkList(member(structuredCell(),1),cell()->GetID(),owner->GetAttributeParameter(type)->GetID());checkList(member(owner->GetAttributeParameter(structType),1),cell()->GetID(),owner->GetAttributeParameter(type)->GetID());
     CK_ID actualObject=0;objectCells->GetElementValue(0,0,&actualObject);check(actualObject==member(structuredCell(),1)->GetID(),"raw object array cell rebound to structured member");
     checkObject(obj,target());checkObject(child(0),target());checkList(list,cell()->GetID(),owner->GetAttributeParameter(type)->GetID());checkList(child(1),cell()->GetID(),owner->GetAttributeParameter(type)->GetID());
    }
   }
  }
  fprintf(log,"RESULT checks=%u failures=%u\n",checks,failures);fclose(log);log=nullptr;m_BML->ExitGame();
 }
};
IMod *BMLEntry(IBML *b){return new ReferenceProbe(b);}void BMLExit(IMod *m){delete m;}
