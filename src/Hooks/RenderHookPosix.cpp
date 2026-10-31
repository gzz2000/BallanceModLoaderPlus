#include "Hooks/RenderHook.h"
#include "CKRenderContext.h"

namespace {
// The native host cooperates instead of patching a platform-specific vtable.
// As in the Windows hook, requests coalesce until the next Render attempt.
CKRenderContext *negotiatedContext = nullptr;
CKRenderContext *attachedContext = nullptr;
RenderHook *attachedOwner = nullptr;
bool skipRequested = false;
}

extern "C" __attribute__((visibility("default"))) CKERROR
BML_SetPlayerDrivenRenderSkipV1(CKRenderContext *context, CKBOOL enable) {
    if (!context) return CKERR_INVALIDPARAMETER;
    if (enable) {
        if ((negotiatedContext && negotiatedContext != context) ||
            (attachedContext && attachedContext != context)) return CKERR_INVALIDPARAMETER;
        negotiatedContext = context;
        skipRequested = false;
    } else if (negotiatedContext == context) {
        negotiatedContext = nullptr;
        skipRequested = false;
    }
    return CK_OK;
}

extern "C" __attribute__((visibility("default"))) CKBOOL
BML_ConsumePlayerRenderSkipV1(CKRenderContext *context) {
    if (!context || context != negotiatedContext || context != attachedContext)
        return FALSE;
    const bool requested = skipRequested;
    skipRequested = false;
    return requested ? TRUE : FALSE;
}

struct RenderHook::Impl {};

RenderHook::RenderHook() : m_Impl(std::make_unique<Impl>()) {}
RenderHook::~RenderHook() { Detach(); }

bool RenderHook::Attach(CKRenderContext *context) {
    if (!context || (attachedOwner && attachedOwner != this) ||
        (attachedContext && attachedContext != context)) return false;
    attachedOwner = this;
    attachedContext = context;
    skipRequested = false;
    return IsAttached();
}

bool RenderHook::Detach() {
    if (attachedOwner == this) {
        if (negotiatedContext == attachedContext) negotiatedContext = nullptr;
        attachedOwner = nullptr;
        attachedContext = nullptr;
        skipRequested = false;
    }
    return true;
}

bool RenderHook::IsAttached() const {
    return attachedOwner == this && IsSkipRenderAvailable();
}

bool RenderHook::IsSkipRenderAvailable() {
    return negotiatedContext && negotiatedContext == attachedContext;
}

void RenderHook::SkipNextRender() {
    if (IsSkipRenderAvailable()) skipRequested = true;
}

void RenderHook::EnableWidescreenFix(bool) {}
bool RenderHook::IsWidescreenFixAvailable() { return false; }
