#include "../TimelineObjects.h"
#include "RCK2dEntity.h"
#include "RCKMaterial.h"
#include "RCKRenderManager.h"
#include "CKRenderCallbackSnapshot.h"
#include <cstdio>
#include <cstdlib>
extern void SetProcessorSpecific_FunctionsPtr();
static unsigned checks;
static void Require(bool ok, const char *name) {
    ++checks;
    if (!ok) { std::fprintf(stderr, "FAIL %s\n", name); std::exit(1); }
}
static int Draw(CKRenderContext *, CKRenderObject *, void *) { return 1; }
int main() {
    SetProcessorSpecific_FunctionsPtr(); Require(CKStartUp() == CK_OK, "startup");
    CKCLASSREGISTERCID(RCKRenderObject, CKCID_BEOBJECT);
    CKCLASSREGISTERCID(RCK2dEntity, CKCID_RENDEROBJECT);
    CKCLASSREGISTERCID(RCKMaterial, CKCID_BEOBJECT);
    CKBuildClassHierarchyTable();
    CKContext *ctx = nullptr; Require(CKCreateContext(&ctx, nullptr, 0, 0) == CK_OK, "context");
    auto *manager = new RCKRenderManager(ctx);
    auto *scene = static_cast<CKScene *>(ctx->CreateObject(CKCID_SCENE, "retained scene"));
    auto *parent = static_cast<RCK2dEntity *>(ctx->CreateObject(CKCID_2DENTITY, "retained parent"));
    auto *material = static_cast<CKMaterial *>(ctx->CreateObject(CKCID_MATERIAL, "shared retained material"));
    auto *group = static_cast<CKGroup *>(ctx->CreateObject(CKCID_GROUP, "retained group"));
    auto *array = static_cast<CKDataArray *>(ctx->CreateObject(CKCID_DATAARRAY, "retained array"));
    auto *parameter = ctx->CreateCKParameterOut("retained reference", CKPGUID_BEOBJECT, TRUE);
    auto *script = static_cast<CKBehavior *>(ctx->CreateObject(CKCID_BEHAVIOR, "retained script", CK_OBJECTCREATION_DYNAMIC));
    Require(scene && parent && material && group && array && parameter && script, "objects");
    script->SetType(CKBEHAVIORTYPE_SCRIPT);
    array->InsertColumn(0, CKARRAYTYPE_OBJECT, "object"); array->AddRow();
    scene->AddObject(parent); parent->SetMaterial(material);
    const int sceneBase = scene->GetObjectCount();
    const int childBase = parent->GetChildrenCount();
    int argument=1; parent->AddPostRenderCallBack(Draw, &argument, FALSE);
    CKRenderCallbackSnapshot callbacks=nullptr;
    Require(CKCaptureRenderCallbacks(ctx, &callbacks)==CK_OK, "captured callbacks");
    for (int iteration=0; iteration<8; ++iteration) {
        auto *copy=static_cast<RCK2dEntity *>(ctx->CreateObject(CKCID_2DENTITY, "arbitrary new object", CK_OBJECTCREATION_DYNAMIC));
        auto *owned=static_cast<CKMaterial *>(ctx->CreateObject(CKCID_MATERIAL, "arbitrary new resource", CK_OBJECTCREATION_DYNAMIC));
        Require(copy && owned, "new objects");
        copy->SetParent(reinterpret_cast<CK2dEntity *>(parent));
        copy->SetMaterial(iteration%2 ? material : owned);
        scene->AddObject(copy); scene->AddObject(owned); group->AddObject(copy);
        CK_ID copyId=copy->GetID(), ownedId=owned->GetID(); parameter->SetValue(&copyId);
        array->SetElementObject(0,0,copy);
        copy->AddPostRenderCallBack(Draw,&argument,TRUE);
        PracticeTimeline::Identities eligible{{copyId,copy},{ownedId,owned}};
        std::size_t removed=99;
        Require(ctx->m_DependenciesContext.GetDependenciesMap().Size()==0, "no pending deletion");
        if (iteration==0) {
            Require(copy->AddScript(script)==CK_OK, "attach saved script to new object");
            const int sceneBeforeReject=scene->GetObjectCount();
            Require(PracticeTimeline::DeleteNewObjects(ctx,eligible,removed)!=CK_OK && removed==0,
                    "reject mandatory dependency on saved object");
            Require(ctx->GetObject(copyId)==copy && ctx->GetObject(ownedId)==owned &&
                    ctx->GetObject(script->GetID())==script && !copy->IsToBeDeleted(), "rejection has no deletion side effects");
            Require(scene->GetObjectCount()==sceneBeforeReject && parent->GetChildrenCount()==childBase+1,
                    "rejection preserves scene and hierarchy");
            Require(copy->RemoveScript(script->GetID())==script, "detach saved script");
            ctx->m_DeferDestroyObjects=TRUE;
            Require(PracticeTimeline::DeleteNewObjects(ctx,eligible,removed)!=CK_OK && removed==0, "reject deferred phase");
            ctx->m_DeferDestroyObjects=FALSE;
        }
        Require(PracticeTimeline::DeleteNewObjects(ctx,eligible,removed)==CK_OK && removed==2, "delete new ownership closure");
        Require(!ctx->GetObject(copyId) && !ctx->GetObject(ownedId), "identities gone");
        Require(scene->GetObjectCount()==sceneBase, "scene membership removed");
        Require(parent->GetChildrenCount()==childBase, "parent child reference removed");
        Require(group->GetObjectCount()==0, "group reference removed");
        Require(!parameter->GetValueObject(FALSE) && !array->GetElementObject(0,0), "parameter and array references cleared");
        Require(ctx->GetObject(material->GetID())==material && parent->GetMaterial()==material, "shared saved material survives");
        Require(manager->m_TemporaryPostRenderCallbacks.Size()==0, "temporary callback queue cleaned");
        Require(CKValidateRenderCallbacks(ctx,callbacks)==CK_OK && CKRestoreRenderCallbacks(ctx,callbacks)==CK_OK,
                "saved callbacks remain valid");
        if(iteration==3) {
            CKDestroyRenderCallbacks(callbacks); callbacks=nullptr;
            Require(CKCaptureRenderCallbacks(ctx,&callbacks)==CK_OK, "overwrite callback snapshot");
        }
    }
    CKDestroyRenderCallbacks(callbacks);
    std::size_t removed=99;
    Require(PracticeTimeline::DeleteNewObjects(ctx,{},removed)==CK_OK && removed==0, "empty rollback");
    Require(CKCloseContext(ctx)==CK_OK,"close"); Require(CKShutdown()==CK_OK,"shutdown");
    std::printf("RESULT checks=%u failures=0\n",checks);
}
