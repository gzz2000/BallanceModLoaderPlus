#include "HUD/SRTimer.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

void SRTimer::Reset() {
    m_Time = 0.0;
    m_Running = false;
    m_Dirty = true;
}

void SRTimer::Start() {
    m_Running = true;
}

void SRTimer::Pause() {
    m_Running = false;
}

void SRTimer::Update(float deltaTimeMs, bool recordedArithmetic) {
    // Recorded clocks retain legacy float accumulation for TAS compatibility;
    // ordinary play uses the native double accumulator. The owner derives this
    // from the toolbox clock, so restoring that clock restores the policy too.
    // Retain the explicit diagnostic override for independent A/B experiments.
    static const int overrideArithmetic = [] {
        const char *value = std::getenv("BALLANCE_DIAG_LEGACY_SR_TIMER");
        return value ? (std::strcmp(value, "1") == 0 ? 1 : 0) : -1;
    }();
    const bool legacyArithmetic = overrideArithmetic < 0 ? recordedArithmetic : overrideArithmetic != 0;
    if (m_Running) {
        if (legacyArithmetic)
            m_Time = static_cast<float>(static_cast<float>(m_Time) + deltaTimeMs);
        else
            m_Time += static_cast<double>(deltaTimeMs);
        m_Dirty = true;
    }
}

void SRTimer::SetState(float elapsedTimeMs, bool running) {
    SetPreciseState(static_cast<double>(elapsedTimeMs), running);
}

void SRTimer::SetPreciseState(double elapsedTimeMs, bool running) {
    m_Time = elapsedTimeMs >= 0.0 ? elapsedTimeMs : 0.0;
    m_Running = running;
    m_Dirty = true;
}

float SRTimer::GetTime() const {
    return (float)m_Time;
}

bool SRTimer::IsRunning() const {
    return m_Running;
}

const char *SRTimer::GetFormattedTime() const {
    UpdateFormattedTime();
    return m_FormattedTime;
}

void SRTimer::UpdateFormattedTime() const {
    if (!m_Dirty) return;

    int64_t totalMs = (int64_t)m_Time;

    int h = (int)(totalMs / 3600000);
    totalMs %= 3600000;
    int m = (int)(totalMs / 60000);
    totalMs %= 60000;
    int s = (int)(totalMs / 1000);
    int ms = (int)(totalMs % 1000);

    std::snprintf(m_FormattedTime, sizeof(m_FormattedTime),
                  "  %02d:%02d:%02d.%03d", h, m, s, ms);
    m_Dirty = false;
}
