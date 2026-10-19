# Practice Tools

Native macOS practice utilities for Ballanced/BML+.

- Hold `Tab` to temporarily bypass an explicit practice frame cap and return
  the deterministic scheduler to 264 samples per second. Releasing it restores
  the exact previous frame-limit mode and value.
- Press `F5` to capture an in-memory state of the current level.
- Press `F8` to restore the latest state.

The snapshot includes Virtools parameter and fixed-shape data-array values,
hierarchy-relative 3D entities, 2D HUD geometry and sprite slots, both master
and object animation cursors, scene/script activation, message waits, global
state/synchronization objects, ordered group membership, behavior-graph IO and delayed-link scheduler
state, and IVP transforms, velocities, queued changes, collision enablement and
sleep state. Snapshot value objects are reused by overwrite saves, avoiding a
large deferred delete/recreate pass and keeping repeated saves bounded.
Runtime values and the Virtools absolute simulation clock/process ordinal are
restored in place: behavior wiring and data-array-owned objects are never
rebuilt, so controls remain connected and one snapshot remains repeatable.
The BML+ SR timer value and its running/paused state are restored as well.
Snapshots are discarded whenever the level is loaded, exited or changed and
are never written to disk. All keys can be changed in `PracticeTools.cfg`.
