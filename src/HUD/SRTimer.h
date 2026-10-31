#ifndef BML_SRTIMER_H
#define BML_SRTIMER_H

#include <cstdint>

class SRTimer {
public:
    SRTimer() = default;
    ~SRTimer() = default;

    void Reset();
    void Start();
    void Pause();

    void Update(float deltaTime, bool recordedArithmetic = false);
    void SetState(float elapsedTimeMs, bool running);

    // Full native accumulator for in-memory rewind; GetTime keeps the legacy float API.
    double GetPreciseTime() const { return m_Time; }
    void SetPreciseState(double elapsedTimeMs, bool running);

    float GetTime() const;
    const char *GetFormattedTime() const;

    bool IsRunning() const;
    bool IsDirty() const { return m_Dirty; }
    void ClearDirty() { m_Dirty = false; }

private:
    double m_Time = 0.0;                  // Accumulated time in milliseconds
    bool m_Running = false;
    mutable char m_FormattedTime[32] = {};
    mutable bool m_Dirty = true;

    void UpdateFormattedTime() const;
};

#endif // BML_SRTIMER_H
