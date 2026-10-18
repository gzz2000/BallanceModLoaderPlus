#include "Hooks/RenderHook.h"

struct RenderHook::Impl {};

RenderHook::RenderHook() : m_Impl(std::make_unique<Impl>()) {}
RenderHook::~RenderHook() = default;

bool RenderHook::Attach(CKRenderContext *) { return false; }
bool RenderHook::Detach() { return true; }
bool RenderHook::IsAttached() const { return false; }
bool RenderHook::IsSkipRenderAvailable() { return false; }
void RenderHook::SkipNextRender() {}
void RenderHook::EnableWidescreenFix(bool) {}
bool RenderHook::IsWidescreenFixAvailable() { return false; }
