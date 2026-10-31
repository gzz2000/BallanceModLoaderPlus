#include "../ParameterReferences.h"
#include <cstdio>
#include <cstdlib>
extern void SetProcessorSpecific_FunctionsPtr();
static unsigned checks;
static void Require(bool ok, const char *what) {
 ++checks; if (!ok) {fprintf(stderr,"FAIL %s\n",what);exit(1);}
}
int main(){
 SetProcessorSpecific_FunctionsPtr();Require(CKStartUp()==CK_OK,"startup");
 CKContext *ctx=nullptr;Require(CKCreateContext(&ctx,nullptr,0,0)==CK_OK,"context");
 auto *target=ctx->CreateCKParameterOut("old target",CKPGUID_INT);CK_ID old=target->GetID();
 auto *obj=ctx->CreateCKParameterOut("object",CKPGUID_OBJECT);
 auto *copy=ctx->CreateCKParameterOut("managed snapshot",CKPGUID_OBJECT);
 obj->SetValue(&old);copy->CopyValue(obj,FALSE);
 auto *list=ctx->CreateCKParameterOut("collection",CKPGUID_OBJECTARRAY);
 XObjectArray *v=nullptr;list->GetValue(&v,FALSE);v->PushBack(old);v->PushBack(0);v->PushBack(old);
 auto *integer=ctx->CreateCKParameterOut("integer equals an ID",CKPGUID_INT);integer->SetValue(&old);
 PracticeTimeline::ParameterReferences o,l,i;
 Require(PracticeTimeline::CaptureParameterReferences(obj,o)==CK_OK,"capture object");
 Require(PracticeTimeline::CaptureParameterReferences(list,l)==CK_OK,"capture collection");
 Require(PracticeTimeline::CaptureParameterReferences(integer,i)==CK_OK,"capture ordinary integer");
 auto *replacement=ctx->CreateCKParameterOut("replacement",CKPGUID_INT);
 Require(replacement->GetID()!=old,"replacement has distinct object ID");
 ctx->DestroyObject(target);
 CK_ID raw=1;copy->GetValue(&raw,FALSE);Require(raw==0,"CK-managed saved object reference cleared on deletion");
 Require((*v)[0]==0&&(*v)[2]==0,"CK-managed collection references cleared on deletion");
 Require(o.targets[0]==old&&l.targets[0]==old&&l.targets[2]==old,"immutable saved typed IDs survived deletion");
 auto resolve=[&](CK_ID id)->CKObject*{return id==old?replacement:nullptr;};
 for(int pass=0;pass<8;++pass){
  obj->CopyValue(copy,FALSE);
  Require(PracticeTimeline::RestoreParameterReferences(obj,o,resolve)==CK_OK,"restore object");
  obj->GetValue(&raw,FALSE);Require(raw==replacement->GetID(),"mapped object value");
  Require(PracticeTimeline::RestoreParameterReferences(list,l,resolve)==CK_OK,"restore collection");
  Require(v->Size()==3&&(*v)[0]==replacement->GetID()&&(*v)[1]==0&&(*v)[2]==replacement->GetID(),"collection preserves null order duplicates");
  Require(PracticeTimeline::RestoreParameterReferences(integer,i,resolve)==CK_OK,"restore integer");
  integer->GetValue(&raw,FALSE);Require(raw==old,"integer not interpreted as reference");
 }
 auto missing=[](CK_ID)->CKObject*{return nullptr;};
 Require(PracticeTimeline::RestoreParameterReferences(list,l,missing)!=CK_OK,"missing nonzero reference rejected");
 Require(v->Size()==3&&(*v)[0]==replacement->GetID(),"invalid collection target leaves previous payload");
 XArray<CKGUID> types;types.PushBack(CKPGUID_OBJECT);types.PushBack(CKPGUID_OBJECTARRAY);
 const CKGUID guid(0x73b75911,0x5a040002);
 Require(ctx->GetParameterManager()->RegisterNewStructure(guid,"reference unit structure","Object,Collection",types)==CK_OK,"structure type");
 auto *structure=ctx->CreateCKParameterOut("old structure",guid);
 auto child=[&](CKParameter *p,int n){return CKParameter::Cast(ctx->GetObject(static_cast<const CK_ID*>(p->GetReadDataPtr())[n]));};
 child(structure,0)->SetValue(&old);XObjectArray *sv=nullptr;child(structure,1)->GetValue(&sv,FALSE);sv->PushBack(old);
 PracticeTimeline::ParameterReferences tree;Require(PracticeTimeline::CaptureParameterReferences(structure,tree)==CK_OK,"capture tree");
 auto *rebuilt=ctx->CreateCKParameterOut("rebuilt structure",guid);
 PracticeTimeline::RebuiltParameters bindings;
 Require(PracticeTimeline::BindParameterReferenceTree(ctx,tree,rebuilt,bindings)==CK_OK,"bind tree");
 Require(bindings.at(tree.id)==rebuilt&&bindings.at(tree.members[0].id)==child(rebuilt,0)&&bindings.at(tree.members[1].id)==child(rebuilt,1),"recursive semantic child IDs");
 Require(PracticeTimeline::RestoreParameterReferences(rebuilt,tree,resolve)==CK_OK,"restore nested typed values");
 child(rebuilt,0)->GetValue(&raw,FALSE);Require(raw==replacement->GetID(),"nested object mapped");
 child(rebuilt,1)->GetValue(&sv,FALSE);Require(sv->Size()==1&&(*sv)[0]==replacement->GetID(),"nested collection mapped");
 CKCloseContext(ctx);CKShutdown();printf("PASS %u typed-reference checks\n",checks);
}
