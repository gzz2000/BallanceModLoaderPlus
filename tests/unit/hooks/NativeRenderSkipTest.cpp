#include "Hooks/RenderHook.h"
#include "CKRenderContext.h"
#include <cstddef>
#include <cstdio>
#include <cstdlib>

extern "C" CKERROR BML_SetPlayerDrivenRenderSkipV1(CKRenderContext *, CKBOOL);
extern "C" CKBOOL BML_ConsumePlayerRenderSkipV1(CKRenderContext *);

static unsigned checks;
static void Require(bool ok, const char *message) {
    ++checks;
    if (!ok) { std::fprintf(stderr,"FAIL %s\n",message); std::exit(1); }
}
int main() {
    // Opaque context identities only; this protocol never dereferences them.
    alignas(std::max_align_t) char firstIdentity[16], secondIdentity[16];
    auto *first=reinterpret_cast<CKRenderContext *>(firstIdentity);
    auto *second=reinterpret_cast<CKRenderContext *>(secondIdentity);
    RenderHook hook, competitor;
    Require(!hook.Attach(nullptr),"null attach");
    Require(BML_SetPlayerDrivenRenderSkipV1(nullptr,TRUE)==CKERR_INVALIDPARAMETER,"null negotiation");
    Require(!hook.Attach(first) && !hook.IsAttached(),"old host remains unsupported");
    RenderHook::SkipNextRender();
    Require(!BML_ConsumePlayerRenderSkipV1(first),"old host request does not latch");
    Require(BML_SetPlayerDrivenRenderSkipV1(first,TRUE)==CK_OK,"negotiate after attach");
    Require(hook.IsAttached() && RenderHook::IsSkipRenderAvailable(),"negotiated capability");
    Require(!competitor.Attach(first),"single active hook owner");
    RenderHook::SkipNextRender();
    Require(!hook.Attach(second),"same owner cannot silently switch contexts");
    Require(hook.IsAttached(),"rejected context switch preserves attachment");
    Require(BML_ConsumePlayerRenderSkipV1(first),"rejected context switch preserves request");
    Require(BML_SetPlayerDrivenRenderSkipV1(second,TRUE)!=CK_OK,"reject competing context");
    for(int i=0;i<128;++i) {
        Require(!BML_ConsumePlayerRenderSkipV1(first),"unrequested render allowed");
        for(int request=0;request<=i;++request) RenderHook::SkipNextRender();
        Require(!BML_ConsumePlayerRenderSkipV1(second),"other context does not consume request");
        Require(BML_ConsumePlayerRenderSkipV1(first),"one render skipped");
        Require(!BML_ConsumePlayerRenderSkipV1(first),"coalesced request consumed exactly once");
    }
    RenderHook::SkipNextRender();
    Require(competitor.Detach(),"unowned detach");
    Require(BML_ConsumePlayerRenderSkipV1(first),"unowned detach keeps active request");
    RenderHook::SkipNextRender();
    Require(hook.Detach(),"detach");
    Require(!RenderHook::IsSkipRenderAvailable() && !BML_ConsumePlayerRenderSkipV1(first),"detach clears capability and request");
    Require(BML_SetPlayerDrivenRenderSkipV1(second,TRUE)==CK_OK,"new context negotiated");
    {
        RenderHook replacement;
        Require(replacement.Attach(second),"negotiation before attach");
        RenderHook::SkipNextRender();
        Require(BML_SetPlayerDrivenRenderSkipV1(second,FALSE)==CK_OK,"host shutdown");
        Require(!replacement.IsAttached() && !BML_ConsumePlayerRenderSkipV1(second),"shutdown clears request");
        Require(BML_SetPlayerDrivenRenderSkipV1(second,TRUE)==CK_OK,"restart handshake");
        RenderHook::SkipNextRender();
    }
    Require(!RenderHook::IsSkipRenderAvailable() && !BML_ConsumePlayerRenderSkipV1(second),"destructor clears stale identities");
    std::printf("RESULT checks=%u failures=0\n",checks);
}
