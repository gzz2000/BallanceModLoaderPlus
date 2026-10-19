# Practice Tools

Native macOS practice utilities for Ballanced/BML+.

- Hold `Tab` to temporarily bypass an explicit practice frame cap and return
  the deterministic scheduler to 264 samples per second. Releasing it restores
  the exact previous frame-limit mode and value.
- Press `F5` to capture an in-memory state of the current level.
- Press `F8` to restore the latest state.

The snapshot includes Virtools behavior, parameter, data-array, 3D entity,
animation and sound state together with IVP transforms, linear/angular
velocities, queued velocity changes, collision enablement and sleep state.
The BML+ SR timer value and its running/paused state are restored as well.
The 2D object hierarchy is deliberately left intact; its visible state is
reconstructed from the restored gameplay parameters and behaviors.
Snapshots are discarded whenever the level is loaded, exited or changed and
are never written to disk. All keys can be changed in `PracticeTools.cfg`.
