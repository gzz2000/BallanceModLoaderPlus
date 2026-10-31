#include "UI/Overlay.h"

#include "UI/Ime/Presentation.h"
#include "UI/OverlayPlatformInput.h"

#include <cstdlib>

#include "CKContext.h"
#include "imgui_internal.h"
#include "UI/imgui_impl_ck2.h"

#if defined(_WIN32) && !BML_USE_SDL3_PLATFORM
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>

#define IMGUI_IMPL_WIN32_DISABLE_GAMEPAD
#include "backends/imgui_impl_win32.h"
#else
#include <SDL3/SDL.h>
#include "backends/imgui_impl_sdl3.h"
#endif

namespace Overlay {
    ImGuiContext *g_ImGuiContext = nullptr;
    bool g_PlatformInitialized = false;
    bool g_RendererInitialized = false;
    bool g_DrawDataReady = false;
    bool g_NewFrame = false;

    static bool IsHeadlessOverlay() {
        const char *value = std::getenv("BALLANCE_HEADLESS");
        return value && value[0] != '\0' && value[0] != '0';
    }

    ImGuiContext *GetImGuiContext() {
        return g_ImGuiContext;
    }

    bool IsImGuiReady() {
        return g_RendererInitialized;
    }

    bool IsImGuiFrameActive() {
        return g_NewFrame;
    }

    ImGuiContext *ImGuiCreateContext() {
        // Setup Dear ImGui context
        IMGUI_CHECKVERSION();

        ImGuiContext *previousContext = ImGui::GetCurrentContext();
        g_ImGuiContext = ImGui::CreateContext();

        ImGuiIO &io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

        ImGui::SetCurrentContext(previousContext);

        return g_ImGuiContext;
    }

    void ImGuiDestroyContext() {
        ImGuiContext *context = g_ImGuiContext;
        if (!context)
            return;

        {
            ImGuiContextScope scope;
            ImGuiEndFrame();
        }
        ImGuiShutdownRenderer(nullptr);
        ImGuiShutdownPlatform(nullptr);

        g_ImGuiContext = nullptr;
        g_DrawDataReady = false;
        g_NewFrame = false;

        ImGui::DestroyContext(context);
    }

    bool ImGuiInitPlatform(CKContext *context) {
        ImGuiContextScope scope;

        // Deterministic physics/TAS CI uses the null rasterizer and has no
        // desktop window.  Keep the ImGui context alive because BML and mods
        // legitimately use its key/state API, but do not initialize a window
        // backend or attach platform input. TAS input is supplied through the
        // CK input hook in this mode.
        if (IsHeadlessOverlay())
            return true;

#if defined(_WIN32) && !BML_USE_SDL3_PLATFORM
        HWND window = context ? static_cast<HWND>(context->GetMainWindow()) : nullptr;
        const bool initialized = window && ImGui_ImplWin32_Init(window);
#else
        SDL_Window *window = context ? static_cast<SDL_Window *>(context->GetMainWindow()) : nullptr;
        const bool initialized = window && ImGui_ImplSDL3_InitForOther(window);
#endif
        if (!initialized)
            return false;
        if (!PlatformInput::Attach(window)) {
#if defined(_WIN32) && !BML_USE_SDL3_PLATFORM
            ImGui_ImplWin32_Shutdown();
#else
            ImGui_ImplSDL3_Shutdown();
#endif
            return false;
        }

        g_PlatformInitialized = true;
        return true;
    }

    bool ImGuiInitRenderer(CKContext *context) {
        ImGuiContextScope scope;

        if (!ImGui_ImplCK2_Init(context))
            return false;

        g_DrawDataReady = false;
        g_RendererInitialized = true;

        return true;
    }

    void ImGuiShutdownPlatform(CKContext *context) {
        (void) context;
        if (!g_PlatformInitialized) {
            PlatformInput::Detach();
            return;
        }

        ImGuiContextScope scope;

        PlatformInput::Detach();
#if defined(_WIN32) && !BML_USE_SDL3_PLATFORM
        ImGui_ImplWin32_Shutdown();
#else
        ImGui_ImplSDL3_Shutdown();
#endif
        g_PlatformInitialized = false;
    }

    void ImGuiShutdownRenderer(CKContext *context) {
        (void) context;
        if (!g_RendererInitialized)
            return;

        ImGuiContextScope scope;

        ImGui_ImplCK2_Shutdown();

        g_DrawDataReady = false;
        g_RendererInitialized = false;
    }

    void ImGuiNewFrame() {
        if (g_RendererInitialized && !g_NewFrame) {
            g_DrawDataReady = false;

            // ImGui's counters are the source of truth. If a Virtools manager
            // callback escaped before Render(), finish that old frame before
            // starting the next one instead of hitting imgui.cpp's hard
            // "Forgot to call Render() or EndFrame()" assertion.
            ImGuiContext *context = ImGui::GetCurrentContext();
            if (context && context->FrameCount != 0 &&
                context->FrameCountEnded != context->FrameCount) {
                ImGui::EndFrame();
            }

#if defined(_WIN32) && !BML_USE_SDL3_PLATFORM
            if (g_PlatformInitialized)
                ImGui_ImplWin32_NewFrame();
#else
            if (g_PlatformInitialized)
                ImGui_ImplSDL3_NewFrame();
#endif
            ImGui_ImplCK2_NewFrame();
            ImGui::NewFrame();

            g_NewFrame = true;
        }
    }

    void ImGuiEndFrame() {
        if (g_NewFrame) {
            ImGui::EndFrame();
            g_NewFrame = false;
        }
    }

    void ImGuiRender() {
        if (g_NewFrame) {
            if (ImGuiContext *context = ImGui::GetCurrentContext())
                Ime::Presentation::Draw(context->PlatformImeData);
            ImGui::Render();
            // Render() completes the ImGui frame. Publish that state before a
            // test callback can trigger a re-entrant Virtools reset/shutdown;
            // otherwise OnCKReset sees a phantom open frame and the next
            // NewFrame() asserts that the previous frame was never ended.
            g_NewFrame = false;
            g_DrawDataReady = true;
        }
    }

    void ImGuiOnRender() {
#if defined(BML_NATIVE_BALLANCED)
        if (const char *disabled = std::getenv("BML_NATIVE_DISABLE_OVERLAY_RENDER");
            disabled && disabled[0] != '\0' && disabled[0] != '0')
            return;
#endif
        if (g_DrawDataReady) {
            ImGuiContextScope scope;
            ImGui_ImplCK2_RenderDrawData(ImGui::GetDrawData());
        }
    }
}
