# Native legacy TAS environment adapter

This experimental module replaces the E1717 construction probe. It preserves
helper, Segment 1.2.7 and legacy GUI creation order while routing the original
TAS entry to BallanceTAS's composed project menu through `tasmenu`. `tasplay`
prepares a record using the normal project/session services. Calls use BML's
command lookup and argument vectors, so filenames are never shell fragments.
Command pointers are not retained between actions.

The adapter retains the `Segment` mod ID and configuration; do not load a
second Segment or the old construction probe beside it. Writable TAS records
come from the loader's resolved `TAS` directory. The old record-list fallback
handles missing directories, and unavailable playback reports an error.

Configure this directory with ENGINE_SOURCE_DIR (Ballanced/Source), BML_LIBRARY,
CK2_LIBRARY and VXMATH_LIBRARY pointing to matching native libraries. It emits
LegacyTASRuntime.bmodp. Replay, real menu interaction and unload require testing
together; compilation is not acceptance. Existing component licensing and
provenance notices remain applicable, especially LegacyTASGui/PROVENANCE.md.
This module has not been included in a release package.

PracticeTools checkpoints use `SegmentCheckpoint.h` and the versioned
`BML_LegacySegmentCapture/Validate/Restore` exports to preserve the separate
Segment timer, sector, counting state, current-level records and incremental
display-update cursor/scratch. The main BML SR timer is a different clock.
The provider validates its instance and map generation; PracticeTools pins the
library until discarding the POD checkpoint and restores CK GUI values through
its existing engine snapshot. Restore does not emit checkpoint events, write
configuration or change user appearance/update settings. This is an explicit
adapter for this module, not automatic checkpoint support for arbitrary mods.

E2480 exposed a 24.2424-second split inflation after 6400 repeated samples.
E2483 confirms unchanged no-restore playback; E2485/E2487 cover repeated fan and
cross-checkpoint restores, including rendering during playback. Segment records
match the independent control byte-for-byte. Full corpus and other lifecycle
failure paths remain separate acceptance requirements.
