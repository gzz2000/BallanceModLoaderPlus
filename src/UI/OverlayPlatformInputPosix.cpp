#include "UI/OverlayPlatformInput.h"

#include "UI/Ime/Runtime.h"
#include "UI/Overlay.h"

#include <SDL3/SDL.h>
#include "backends/imgui_impl_sdl3.h"

namespace Overlay::PlatformInput {
namespace {
SDL_Window *g_Window = nullptr;

bool FeedEvent(void *, SDL_Event *event) {
    if (!event || !g_Window)
        return true;
    ImGuiContextScope scope;
    if (scope.IsActive())
        ImGui_ImplSDL3_ProcessEvent(event);
    return true;
}
}

bool Attach(void *window) {
    SDL_Window *sdlWindow = static_cast<SDL_Window *>(window);
    if (!sdlWindow)
        return false;
    if (g_Window == sdlWindow)
        return true;
    if (g_Window)
        Detach();
    g_Window = sdlWindow;
    Ime::Runtime::Attach(sdlWindow, sdlWindow);
    return SDL_AddEventWatch(&FeedEvent, nullptr);
}

bool Detach() {
    if (g_Window)
        SDL_RemoveEventWatch(&FeedEvent, nullptr);
    Ime::Runtime::Detach();
    g_Window = nullptr;
    return true;
}
}
