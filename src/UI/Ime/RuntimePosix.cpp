#include "UI/Ime/Runtime.h"

namespace Overlay::Ime::Runtime {
namespace {
void *g_RootWindow = nullptr;
void *g_PresentationWindow = nullptr;
bool g_Visible = false;
}

void Attach(void *rootWindow, void *presentationWindow) {
    g_RootWindow = rootWindow;
    g_PresentationWindow = presentationWindow;
}

void Detach() {
    g_RootWindow = nullptr;
    g_PresentationWindow = nullptr;
    g_Visible = false;
}

NativePresentation::MessageDisposition HandleNativeMessage(
    void *, std::uint32_t, std::uintptr_t, std::intptr_t) {
    return {};
}

bool WantsCandidateNavigation() { return false; }

bool PreparePresentationFrame(bool visible, PresentationFrame &frame) {
    g_Visible = visible;
    if (!visible)
        frame = {};
    return false;
}

bool IsPresentationActive() { return g_Visible; }

Diagnostics GetDiagnostics() {
    Diagnostics result;
    result.RootWindow = reinterpret_cast<std::uintptr_t>(g_RootWindow);
    result.PresentationWindow = reinterpret_cast<std::uintptr_t>(g_PresentationWindow);
    result.PresentationVisible = g_Visible;
    return result;
}

bool MoveCandidate(CandidateDirection) { return false; }
}
