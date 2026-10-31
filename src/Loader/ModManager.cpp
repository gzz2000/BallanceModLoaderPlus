#include "Loader/ModManager.h"
#include "../../mods/LegacyTASRuntime/Legacy0340/HelperRuntime.h"
#include "../../mods/LegacyTASRuntime/Legacy0340/GuiRuntime.h"
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <exception>

#include "Loader/ModContext.h"

#include "BML/InputHook.h"
#include "Hooks/HookLifecycle.h"
#include "Hooks/InputCursor.h"
#include "Hooks/RenderHook.h"
#include "UI/Overlay.h"
#include "UI/FontRuntime.h"
#if BML_ENABLE_UI_AUTOMATION
#include "player/UiAutomation.h"
#endif

namespace {
class ImGuiFrameCompletion {
public:
    ~ImGuiFrameCompletion() {
        Overlay::ImGuiEndFrame();
    }
};
} // namespace

ModManager::ModManager(CKContext *context) : CKBaseManager(context, MOD_MANAGER_GUID, (CKSTRING) "Mod Manager") {
    m_ModContext = new ModContext(m_Context);
    context->RegisterNewManager(this);
}

ModManager::~ModManager() {
    StopRuntime();
    InvalidateLegacyGuiWorlds();
    m_LegacyHelpers.reset();
    ModContext::Destroy(m_ModContext);
}

CKERROR ModManager::OnCKInit() {
    return m_ModContext->Init() ? CK_OK : CKERR_NOTINITIALIZED;
}

CKERROR ModManager::OnCKEnd() {
    const bool stopped = StopRuntime();
    InvalidateLegacyGuiWorlds();
    m_LegacyHelpers.reset();
    const bool shutdown = m_ModContext->Shutdown();
    return stopped && shutdown ? CK_OK : CKERR_NOTINITIALIZED;
}

CKERROR ModManager::OnCKPlay() {
    if (!m_ModContext || !m_ModContext->IsInited())
        return CKERR_NOTINITIALIZED;
    if (m_Context->IsReseted() && m_Context->GetCurrentLevel() != nullptr &&
        !m_RenderContext) {
        if (m_PlayerDrivenStartup) {
            m_PlayerStartupPending = true;
            m_PlayerStartupTick = m_Context->GetTimeManager()->GetMainTickCount();
        } else if (!StartRuntime()) {
            return CKERR_NOTINITIALIZED;
        }
    }

    return CK_OK;
}

CKERROR ModManager::SetPlayerDrivenStartup(CKBOOL enable) {
    // CK can retain a registered manager after OnCKInit failed. In that case
    // Init has already released the logger, hooks and ImGui context; neither
    // startup path may use those services or accept a deferred start request.
    if (!m_ModContext || !m_ModContext->IsInited())
        return CKERR_NOTINITIALIZED;
    if (m_RenderContext)
        return CKERR_INVALIDPARAMETER;
    m_PlayerDrivenStartup = enable != FALSE;
    m_PlayerStartupPending = false;
    return CK_OK;
}

CKERROR ModManager::CompletePlayerStep() {
    if (!m_ModContext || !m_ModContext->IsInited()) {
        m_PlayerStartupPending = false;
        return CKERR_NOTINITIALIZED;
    }
    if (!m_PlayerDrivenStartup || !m_PlayerStartupPending)
        return CK_OK;
    const CKDWORD tick = m_Context->GetTimeManager()->GetMainTickCount();
    if (!m_Context->IsPlaying() || tick == m_PlayerStartupTick)
        return CKERR_INVALIDPARAMETER;

    m_PlayerStartupPending = false;
    m_ModContext->GetLogger()->Info(
        "Player-driven startup after Process: tick=%u objects=%d",
        static_cast<unsigned>(tick), m_Context->GetObjectCount());
    return StartRuntime() ? CK_OK : CKERR_NOTINITIALIZED;
}

CKERROR ModManager::OnCKReset() {
    const bool stopped = StopRuntime();
    if (m_ModContext)
        m_ModContext->ResetVirtoolsWorld();

    return stopped ? CK_OK : CKERR_NOTINITIALIZED;
}

CKERROR ModManager::PreClearAll() {
    InvalidateLegacyGuiWorlds();
    m_LegacyHelpers.reset();
    if (m_ModContext)
        m_ModContext->ResetVirtoolsWorld();
    return CK_OK;
}

void ModManager::RegisterLegacyGuiWorld(const std::shared_ptr<LegacyBGui::Detail::World> &world) {
    if (!world || world->context != m_Context)
        throw std::logic_error("legacy GUI belongs to another context");
    m_LegacyGuiWorlds.erase(std::remove_if(m_LegacyGuiWorlds.begin(),m_LegacyGuiWorlds.end(),
        [](const auto &weak) { return weak.expired(); }),m_LegacyGuiWorlds.end());
    m_LegacyGuiWorlds.push_back(world);
}

void ModManager::InvalidateLegacyGuiWorlds() noexcept {
    for (auto &weak:m_LegacyGuiWorlds)
        if (auto world=weak.lock()) world->Invalidate();
    m_LegacyGuiWorlds.clear();
}

CKERROR ModManager::SequenceToBeDeleted(CK_ID *objids, int count) {
    for (auto &weak:m_LegacyGuiWorlds)
        if (auto world=weak.lock()) world->Deleting(objids,count);
    if (m_LegacyHelpers)
        m_LegacyHelpers->ObjectsToBeDeleted(objids, count);
    if (m_ModContext)
        m_ModContext->VirtoolsObjectsToBeDeleted(objids, count);
    return CK_OK;
}

CKERROR ModManager::PreProcess() {
    if (!m_ModContext || !m_ModContext->IsInited() || !m_RenderContext)
        return CK_OK;

    Overlay::ImGuiContextScope scope;

    SynchronizeUiFonts();
    Overlay::ImGuiNewFrame();

    return CK_OK;
}

void ModManager::SynchronizeUiFonts() {
    if (!m_ModContext || !m_RenderContext)
        return;

    BML::UI::FontRuntime *fonts = m_ModContext->GetUiFontRuntime();
    ImGuiContext *imgui = Overlay::GetImGuiContext();
    if (!fonts || !imgui)
        return;

    fonts->Synchronize(*imgui, static_cast<float>(m_RenderContext->GetHeight()));
    const BML::UI::FontRuntimeSnapshot &snapshot = fonts->Inspect();
    if (snapshot.Generation == 0 || snapshot.Generation == m_ReportedUiFontGeneration)
        return;

    m_ReportedUiFontGeneration = snapshot.Generation;
    m_ModContext->GetLogger()->Info(
        "Applied UI font generation %llu "
        "(Unicode scalars: %s, common emoji: %s, color emoji: %s)",
        static_cast<unsigned long long>(snapshot.Generation),
        snapshot.SupportsUnicodeScalars ? "yes" : "no",
        snapshot.SupportsCommonEmoji ? "yes" : "no",
        snapshot.SupportsColorEmoji ? "yes" : "no");
    for (const std::string &diagnostic : snapshot.Diagnostics)
        m_ModContext->GetLogger()->Warn("UI font: %s", diagnostic.c_str());
}

bool ModManager::StartRuntime() {
    if (!m_ModContext || !m_ModContext->IsInited())
        return false;
    if (m_RenderContext)
        return true;

    CKRenderContext *renderContext = m_Context->GetPlayerRenderContext();
    if (!renderContext)
        return false;

    const char *assetsOption = std::getenv("BALLANCE_BML_LEGACY_ASSETS");
    const bool loadLegacyAssets = assetsOption && std::strcmp(assetsOption, "1") == 0;
    const char *legacy = std::getenv("BALLANCE_BML_LEGACY_HELPERS");
    if (loadLegacyAssets && (!legacy || std::strcmp(legacy, "1") != 0)) {
        m_ModContext->GetLogger()->Error("Legacy assets require legacy helpers");
        return false;
    }
    if (legacy && std::strcmp(legacy, "1") == 0) {
        if (!m_PlayerDrivenStartup) {
            m_ModContext->GetLogger()->Error("Legacy helpers require Player-driven startup");
            return false;
        }
        try {
            if (!m_LegacyHelpers) {
                m_LegacyHelpers = std::make_unique<LegacyExecuteBB::Runtime>(m_Context);
                m_ModContext->GetLogger()->Info("Legacy helpers initialized: created=%zu objects=%d",
                    m_LegacyHelpers->CreatedObjectIds().size(), m_Context->GetObjectCount());
            } else {
                // Never silently reconstruct into a partially deleted world.
                m_LegacyHelpers->Validate();
            }
            if (loadLegacyAssets) {
                m_LegacyHelpers->LoadAuxiliaryAssets();
                m_ModContext->GetLogger()->Info("Legacy assets initialized: created=%zu objects=%d",
                    m_LegacyHelpers->CreatedObjectIds().size(), m_Context->GetObjectCount());
            }
        } catch (const std::exception &error) {
            m_ModContext->GetLogger()->Error("Legacy helper startup failed: %s", error.what());
            return false;
        }
    }

    if (!m_ModContext->AttachRenderHook(renderContext)) {
        if (RenderHook::IsSkipRenderAvailable()) {
            m_ModContext->GetLogger()->Error("A render hook from an earlier context is still installed");
            return false;
        }
        m_ModContext->GetLogger()->Warn("Render skip is unavailable for this render context");
    }
    if (!RenderHook::IsWidescreenFixAvailable())
        m_ModContext->GetLogger()->Warn("Widescreen projection correction is unavailable for this CK2_3D implementation");

    if (!Overlay::ImGuiInitRenderer(m_Context)) {
        m_ModContext->DetachRenderHook();
        m_ModContext->GetLogger()->Error("Failed to initialize the ImGui renderer backend");
        return false;
    }

    m_RenderContext = renderContext;
    Overlay::ImGuiContextScope scope;
    if (!m_ModContext->GetModLoader().Start()) {
        Overlay::ImGuiShutdownRenderer(m_Context);
        m_ModContext->DetachRenderHook();
        m_RenderContext = nullptr;
        return false;
    }

    SynchronizeUiFonts();
    Overlay::ImGuiNewFrame();
    return true;
}

bool ModManager::StopRuntime() {
    m_PlayerStartupPending = false;
    if (!m_ModContext)
        return true;

    bool stopped = true;
    if (m_RenderContext) {
        Overlay::ImGuiContextScope scope;
        Overlay::ImGuiEndFrame();
        SetOverlayCursorVisible(false);
        stopped = m_ModContext->GetModLoader().Stop();
        Overlay::ImGuiShutdownRenderer(m_Context);
        m_RenderContext = nullptr;
    } else {
        stopped = m_ModContext->GetModLoader().Stop();
    }

    const bool detached = m_ModContext->DetachRenderHook();
    if (!detached && m_ModContext->GetLogger())
        m_ModContext->GetLogger()->Warn("Render skip hook could not be detached cleanly");
    return stopped && detached;
}

CKERROR ModManager::PostProcess() {
    if (!m_ModContext || !m_ModContext->IsInited())
        return CK_OK;

    // Hooks already own the physics callback during initialization. A Player
    // that starts the mod runtime after its first Process must still submit
    // that first physics step, even though the UI is not ready yet.
    if (!m_RenderContext) {
        m_ModContext->RunPhysicsPostProcess();
        return CK_OK;
    }

    // The scope covers the whole frame so that whatever a mod draws goes to the loader's
    // context, but the physics, mod, and input work below is not drawing and still has to
    // run on a frame that has no context to draw into.
    Overlay::ImGuiContextScope scope;
    ImGuiFrameCompletion frameCompletion;

    // The authored pause behavior sets the physics time factor to zero.
    // Still forward the owned callback: its zero-time dispatch maintains
    // physics event/cache state without advancing the simulation clock.
    m_ModContext->RunPhysicsPostProcess();

    m_ModContext->ProcessVirtoolsFrame();

    m_ModContext->OnProcess();

    auto *inputHook = m_ModContext->GetInputManager();

    if (scope.IsActive()) {
        SetOverlayCursorVisible(ImGui::GetIO().WantCaptureMouse);
        Overlay::ImGuiRender();
    } else {
        SetOverlayCursorVisible(false);
    }

    if (inputHook)
        inputHook->Process();
#if BML_ENABLE_UI_AUTOMATION
    // Test actions can reset or exit Virtools. Run them only after Render and
    // after the final per-frame pointer use, never from inside ImGuiRender().
    UiAutomation::AdvanceFrame();
#endif
    return CK_OK;
}

CKERROR ModManager::OnPostRender(CKRenderContext *dev) {
    if (!m_ModContext || !m_ModContext->IsInited() || !m_RenderContext)
        return CK_OK;

    m_ModContext->OnRender(dev);

    return CK_OK;
}

CKERROR ModManager::OnPostSpriteRender(CKRenderContext *dev) {
    if (!m_ModContext || !m_ModContext->IsInited() || !m_RenderContext)
        return CK_OK;

    Overlay::ImGuiOnRender();
    return CK_OK;
}
