#include "../TimelineObjects.h"
#include <cstdio>
#include <cstdlib>
#include <new>
#include <set>

extern void SetProcessorSpecific_FunctionsPtr();
static unsigned checks;
static void Require(bool value, const char *message) {
    ++checks;
    if (!value) { std::fprintf(stderr, "FAIL %s\n", message); std::exit(1); }
}

int main() {
    SetProcessorSpecific_FunctionsPtr();
    Require(CKStartUp() == CK_OK, "startup");
    Require(CKGetObjectLifetimeToken(nullptr, 1) == 0, "null context");
    std::set<std::uint64_t> allTokens;
    for (unsigned round = 0; round < 3; ++round) {
        CKContext *ctx = nullptr;
        Require(CKCreateContext(&ctx, nullptr, 0, 0) == CK_OK, "create context");
        Require(CKGetObjectLifetimeToken(ctx, 0) == 0, "zero id");
        Require(CKGetObjectLifetimeToken(ctx, CK_ID(-1)) == 0, "out of range id");
        alignas(CKObject) unsigned char slot[sizeof(CKObject)];
        CK_ID reused = 0;
        PracticeTimeline::Identities saved;
        PracticeTimeline::LifetimeTokens tokens;
        for (unsigned iteration = 0; iteration < 16; ++iteration) {
            // Force BOTH address and ID reuse; allocator luck cannot make this pass.
            auto *object = new (slot) CKObject(ctx, "same address");
            const CK_ID id = object->GetID();
            const auto token = CKGetObjectLifetimeToken(ctx, id);
            Require(token != 0 && allTokens.insert(token).second, "fresh lifetime token");
            if (iteration) {
                Require(id == reused && ctx->GetObject(id) == saved.at(id), "same id and address really reused");
                Require(!PracticeTimeline::MatchesSavedObject(ctx, id, saved, tokens), "old lifetime rejected");
            }
            reused = id;
            saved[id] = object; tokens[id] = token;
            Require(PracticeTimeline::MatchesSavedObject(ctx, id, saved, tokens), "fresh snapshot accepted");
            object->SetName("changed name"); object->Show(CKHIDE);
            Require(CKGetObjectLifetimeToken(ctx, id) == token, "value mutation preserves identity");
            ctx->m_ObjectManager->StartLoadSession(128);
            ctx->m_ObjectManager->RegisterLoadObject(object, 127);
            Require(ctx->m_ObjectManager->RealId(127) == id &&
                    CKGetObjectLifetimeToken(ctx, id) == token, "load alias preserves identity");
            ctx->m_ObjectManager->EndLoadSession();
            object->ModifyObjectFlags(CK_OBJECT_TOBEDELETED, 0);
            Require(!PracticeTimeline::MatchesSavedObject(ctx, id, saved, tokens), "pending deletion rejected");
            object->ModifyObjectFlags(0, CK_OBJECT_TOBEDELETED);
            Require(CKGetObjectLifetimeToken(ctx, id) == token, "cancelled deletion same lifetime");
            object->ModifyObjectFlags(CK_OBJECT_FREEID, 0);
            object->~CKObject();
            Require(!ctx->GetObject(id) && CKGetObjectLifetimeToken(ctx, id) == 0, "destructor invalidates lifetime");
            Require(!PracticeTimeline::MatchesSavedObject(ctx, id, saved, tokens), "absent identity rejected");
        }
        // Exercise actual Context destruction and dynamic reset, not only direct destructors.
        auto *normal = ctx->CreateObject(CKCID_BEOBJECT, "normal deletion");
        Require(normal != nullptr, "normal object");
        CK_ID id = normal->GetID();
        const auto before = CKGetObjectLifetimeToken(ctx, id);
        Require(before && allTokens.insert(before).second, "normal token");
        Require(ctx->DestroyObject(id, CK_DESTROY_FREEID) == CK_OK, "notified destruction");
        Require(CKGetObjectLifetimeToken(ctx, id) == 0, "notified invalidation");
        auto *dynamic = ctx->CreateObject(CKCID_BEOBJECT, "dynamic reset", CK_OBJECTCREATION_DYNAMIC);
        Require(dynamic && dynamic->GetID() == id, "normal id reuse");
        const auto dynamicToken = CKGetObjectLifetimeToken(ctx, id);
        Require(dynamicToken && dynamicToken != before && allTokens.insert(dynamicToken).second, "reset token unique");
        Require(ctx->m_ObjectManager->OnCKReset() == CK_OK, "dynamic reset");
        Require(!ctx->GetObject(id) && CKGetObjectLifetimeToken(ctx, id) == 0, "reset invalidates lifetime");
        auto *survivor = ctx->CreateObject(CKCID_BEOBJECT, "context shutdown");
        Require(survivor != nullptr, "shutdown object");
        Require(allTokens.insert(CKGetObjectLifetimeToken(ctx, survivor->GetID())).second, "new context does not recycle tokens");
        Require(CKCloseContext(ctx) == CK_OK, "close context with live object");
    }
    Require(CKShutdown() == CK_OK, "shutdown");
    std::printf("RESULT checks=%u failures=0 lifetimes=%zu\n", checks, allTokens.size());
}
