#include "UI/Ime/NativePresentation.h"

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#include <imm.h>
#endif

namespace Overlay::Ime::NativePresentation {
    namespace {
#if defined(_WIN32)
        bool ShouldSuppressNativeNotification(std::uintptr_t notification) noexcept {
            return notification == IMN_OPENCANDIDATE ||
                   notification == IMN_CHANGECANDIDATE ||
                   notification == IMN_CLOSECANDIDATE ||
                   notification == IMN_SETCANDIDATEPOS;
        }
#endif
    }

    MessageDisposition Decide(std::uint32_t message, std::uintptr_t wParam,
                              std::intptr_t lParam, bool ownsPresentation) noexcept {
        MessageDisposition disposition;
        if (!ownsPresentation)
            return disposition;

#if defined(_WIN32)
        if (message == WM_IME_SETCONTEXT && wParam != 0) {
            disposition.replaceLParam = true;
            disposition.lParam = lParam & ~static_cast<std::intptr_t>(ISC_SHOWUIALL);
            return disposition;
        }

        switch (message) {
        case WM_IME_STARTCOMPOSITION:
        case WM_IME_ENDCOMPOSITION:
            disposition.suppress = true;
            break;
        case WM_IME_COMPOSITION:
            disposition.suppress = (lParam & GCS_RESULTSTR) == 0;
            break;
        case WM_IME_NOTIFY:
            disposition.suppress = ShouldSuppressNativeNotification(wParam);
            break;
        default:
            break;
        }
#else
        (void) message;
        (void) wParam;
        (void) lParam;
#endif
        return disposition;
    }
}
