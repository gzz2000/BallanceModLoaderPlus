// Drives the loader's speedrun timer. The interface struct below is what the
// loader hands out and what a Mod written in C uses directly; the inline C++
// namespace under it is the same thing with the lookup and the checks folded in,
// so including this header still costs nothing at link time.
//
// Interface.h explains the header, the version rules, and BML_IFACE_HAS. Every
// function here runs on the calling thread and answers BML_ERROR_WRONG_THREAD
// outside the game thread.
//
// There is exactly one timer, the one the loader shows on its own HUD, and the
// loader keeps driving it: it resets on level start, pauses on level pause and
// when the counter goes inactive, and starts again on unpause and when the
// counter goes active. A mod that starts, pauses, or resets the timer is
// therefore changing shared state that the loader will overwrite at its next
// gameplay event. Read the elapsed time freely; write only if the mod is
// deliberately taking the timer over.
#ifndef BML_SPEEDRUN_H
#define BML_SPEEDRUN_H

#include "BML/Interface.h"

BML_BEGIN_CDECLS

#pragma pack(push, 8)

#define BML_SPEEDRUN_INTERFACE_ID "bml.speedrun"
#define BML_SPEEDRUN_INTERFACE_MAJOR 1
#define BML_SPEEDRUN_INTERFACE_MINOR 2

// ElapsedTime is in milliseconds, and matches IBML::GetSRScore.
// This is a C struct and has no default member
// initializer: zero it with BML_SpeedrunTimerState state = {0} in C, or with
// TimerState state{} in C++.
typedef struct BML_SpeedrunTimerState {
    float ElapsedTime;
} BML_SpeedrunTimerState;

// Interface 1.2: lossless native timer state for in-memory rewind. This is
// separate from the unchanged four-byte TimerState used by older mods.
typedef struct BML_SpeedrunTimerSnapshot {
    double ElapsedTime;
    int Running;
} BML_SpeedrunTimerSnapshot;

typedef struct BML_SpeedrunInterface {
    BML_InterfaceHeader Header;

    // Answers BML_ERROR_INVALID_PARAMETER for a null out and BML_ERROR_FAIL
    // before the loader has initialized. Nothing else fails.
    int (BML_CDECL *ReadTimerState)(BML_SpeedrunTimerState *out);

    // Toggles only the visibility of the HUD timer element, without writing the
    // loader's ShowSR configuration entry. The loader restores the configured
    // value on the next level start, and hides the element on level exit. Use
    // UI::SetHUDMode with HUD_SR for a lasting change.
    int (BML_CDECL *SetTimerVisible)(int visible);

    // Resumes counting from the current elapsed time rather than from zero. Call
    // ResetTimer first for a fresh run.
    int (BML_CDECL *StartTimer)(void);

    // Stops counting and keeps the elapsed time.
    int (BML_CDECL *PauseTimer)(void);

    // Zeroes the elapsed time and leaves the timer stopped.
    int (BML_CDECL *ResetTimer)(void);

    // Reads whether the timer is advancing. Added in interface 1.1.
    int (BML_CDECL *ReadTimerRunning)(int *out);

    // Restores both elapsed milliseconds and whether the timer is advancing.
    // Added in interface 1.1. The original TimerState remains unchanged so
    // Mods compiled against 1.0 keep their four-byte output buffer.
    int (BML_CDECL *WriteTimerState)(float elapsedTime, int running);

    // Added in 1.2. Null pointers, non-finite or negative elapsed time are
    // invalid. Read does not change the accumulator; write restores it exactly.
    int (BML_CDECL *ReadTimerSnapshot)(BML_SpeedrunTimerSnapshot *out);
    int (BML_CDECL *WriteTimerSnapshot)(const BML_SpeedrunTimerSnapshot *state);
} BML_SpeedrunInterface;

#pragma pack(pop)

BML_END_CDECLS

#ifdef __cplusplus

namespace BML::Speedrun {

using TimerState = BML_SpeedrunTimerState;
using TimerSnapshot = BML_SpeedrunTimerSnapshot;

namespace Detail {

// Looked up once per Mod. The loader's table is static, so a null answer means
// the running loader does not carry this interface at all, which is not something
// that can change later in the process.
inline const BML_SpeedrunInterface *Interface() {
    static const BML_SpeedrunInterface *found =
        FindInterface<BML_SpeedrunInterface>(BML_SPEEDRUN_INTERFACE_ID, BML_SPEEDRUN_INTERFACE_MAJOR);
    return found;
}

} // namespace Detail

// Whether the running loader carries this interface. The functions below check
// for themselves, so call this directly only to probe.
[[nodiscard]] inline int RequireApi() {
    return Detail::Interface() != nullptr ? BML_OK : BML_ERROR_NOT_FOUND;
}

[[nodiscard]] inline int ReadTimerState(TimerState &out) {
    const BML_SpeedrunInterface *speedrun = Detail::Interface();
    if (!BML_IFACE_HAS(speedrun, BML_SpeedrunInterface, ReadTimerState))
        return BML_ERROR_NOT_FOUND;
    return speedrun->ReadTimerState(&out);
}

[[nodiscard]] inline int SetTimerVisible(bool visible) {
    const BML_SpeedrunInterface *speedrun = Detail::Interface();
    if (!BML_IFACE_HAS(speedrun, BML_SpeedrunInterface, SetTimerVisible))
        return BML_ERROR_NOT_FOUND;
    return speedrun->SetTimerVisible(visible ? 1 : 0);
}

[[nodiscard]] inline int StartTimer() {
    const BML_SpeedrunInterface *speedrun = Detail::Interface();
    if (!BML_IFACE_HAS(speedrun, BML_SpeedrunInterface, StartTimer))
        return BML_ERROR_NOT_FOUND;
    return speedrun->StartTimer();
}

[[nodiscard]] inline int PauseTimer() {
    const BML_SpeedrunInterface *speedrun = Detail::Interface();
    if (!BML_IFACE_HAS(speedrun, BML_SpeedrunInterface, PauseTimer))
        return BML_ERROR_NOT_FOUND;
    return speedrun->PauseTimer();
}

[[nodiscard]] inline int ResetTimer() {
    const BML_SpeedrunInterface *speedrun = Detail::Interface();
    if (!BML_IFACE_HAS(speedrun, BML_SpeedrunInterface, ResetTimer))
        return BML_ERROR_NOT_FOUND;
    return speedrun->ResetTimer();
}

[[nodiscard]] inline int ReadTimerRunning(bool &running) {
    const BML_SpeedrunInterface *speedrun = Detail::Interface();
    if (!BML_IFACE_HAS(speedrun, BML_SpeedrunInterface, ReadTimerRunning))
        return BML_ERROR_NOT_FOUND;
    int value = 0;
    const int status = speedrun->ReadTimerRunning(&value);
    if (status == BML_OK)
        running = value != 0;
    return status;
}

[[nodiscard]] inline int WriteTimerState(float elapsedTime, bool running) {
    const BML_SpeedrunInterface *speedrun = Detail::Interface();
    if (!BML_IFACE_HAS(speedrun, BML_SpeedrunInterface, WriteTimerState))
        return BML_ERROR_NOT_FOUND;
    return speedrun->WriteTimerState(elapsedTime, running ? 1 : 0);
}

[[nodiscard]] inline int ReadTimerSnapshot(TimerSnapshot &out) {
    const BML_SpeedrunInterface *speedrun = Detail::Interface();
    if (!BML_IFACE_HAS(speedrun, BML_SpeedrunInterface, ReadTimerSnapshot))
        return BML_ERROR_NOT_FOUND;
    return speedrun->ReadTimerSnapshot(&out);
}

[[nodiscard]] inline int WriteTimerSnapshot(const TimerSnapshot &state) {
    const BML_SpeedrunInterface *speedrun = Detail::Interface();
    if (!BML_IFACE_HAS(speedrun, BML_SpeedrunInterface, WriteTimerSnapshot))
        return BML_ERROR_NOT_FOUND;
    return speedrun->WriteTimerSnapshot(&state);
}

} // namespace BML::Speedrun

#endif // __cplusplus

#endif // BML_SPEEDRUN_H
