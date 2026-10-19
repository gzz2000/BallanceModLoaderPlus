# Practice Tools

Native macOS practice utilities for Ballanced/BML+.

- Hold `Tab` to temporarily bypass an explicit practice frame cap and return
  the deterministic scheduler to 264 samples per second. Releasing it restores
  the exact previous frame-limit mode and value.
- Press `F5` to capture an in-memory state of the current level.
- Press `F8` to restore the latest state.

The snapshot includes Virtools parameter values, fixed-shape data-array values
and 3D entity state together with IVP transforms, linear/angular velocities,
queued velocity changes, collision enablement and sleep state. Runtime values
are restored in place: behavior wiring and data-array-owned objects are never
rebuilt, so controls remain connected and the same snapshot can be restored
repeatedly.
The BML+ SR timer value and its running/paused state are restored as well.
The 2D object hierarchy is deliberately left intact; its visible state is
reconstructed from the restored gameplay parameters and behaviors.
Snapshots are discarded whenever the level is loaded, exited or changed and
are never written to disk. All keys can be changed in `PracticeTools.cfg`.
