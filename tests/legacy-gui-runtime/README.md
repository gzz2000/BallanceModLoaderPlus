# Legacy GUI lifecycle regression

This is an opt-in game-hosted test module, not a gameplay mod. Run only in an
isolated copy of the game and user data. It loads the real legacy helper, asset,
Segment and TAS GUI constructors; it neither bundles game data nor replaces the
active TAS replay module. Do not install alongside another Segment module.

Configure with `ENGINE_SOURCE_DIR` pointing to Ballanced's `Source` directory,
and `BML_LIBRARY`, `CK2_LIBRARY`, `VXMATH_LIBRARY` pointing to the matching native
libraries, then build the `LegacyGuiLifecycleProbe` target. The BML library must
include `Legacy0340/GuiRuntime.cpp` and `Gui.cpp`.

Use the Player CMO-restart command bridge and disable the host's optional legacy
helper/asset stages: this test constructs its own instance. Set
`BALLANCE_E1697_RESTARTS_SENT=0` for nine composition loads, or `8` for one load.
Allow startup and a bounded automatic quit. Do not enable TAS AutoLoad for this
test. It deliberately creates/deletes fixture objects after the constructor dump
and is not suitable for a physics-neutrality trace.

A passing nine-load run has nine `GUI_LIFETIME_PASS` records, eight
`GUI_RELOAD_QUEUED` records, nine completed CMO loads, normal process exit, and no
ASan or engine initialization errors. Check the actual mapped library hashes;
an injected ASan runtime alone does not prove CK2 instrumentation. Failure exits
with status 42 and a `GUI_LIFETIME_FAIL` message.

The fixture covers all wrapper destructor branches, duplicate factory entries,
callback invocation, exact live-object and owner-parameter cleanup, real CK_ID
reuse, rejection of stale references, and safe stale-reference destruction.
Raw inventories and graph dumps permit constructor comparisons. These checks do
not establish complete old BML gameplay semantics, TAS parity, or whole-world
save/restore correctness.
