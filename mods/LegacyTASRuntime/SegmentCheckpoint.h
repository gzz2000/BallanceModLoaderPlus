#pragma once
#include <cstddef>
#include <cstdint>

// Same-process ABI. No owned pointers or module destructors in checkpoint data.
// The caller must keep the provider library loaded until it discards the state.
// Restore the CK GUI objects separately; this stores the private Segment logic.
struct LegacySegmentCheckpointV1 {
    std::uint32_t size = sizeof(LegacySegmentCheckpointV1);
    std::uint32_t version = 1;
    std::uint64_t instance = 0, levelEpoch = 0;
    double elapsed = 0;
    double records[9]{};
    long long dutyCursor = 0;
    int level = 0, sector = 0, sectorCount = 0;
    bool counting = false, irregular = false, customMap = false;
    // delta and text buffers may contain unused scratch bytes before their
    // duty slice runs. Preserve their representation without evaluating them.
    unsigned char delta[sizeof(double)]{};
    char timeText[50]{}, deltaText[50]{};
};
