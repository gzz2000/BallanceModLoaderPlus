# DebugUtilities for BallanceWeb

Optional dynamic port of BML Plus's bundled DebugUtilities 0.3.2 (MIT).
Import `DebugUtilities.bwmod` in Settings → User data → Plugins, then restart.
Use BallanceWeb 20261103.2 or later: that host also fixes Space input in the
command bar. The binary uses ABI `ballanceweb-cxx-wasm32-v1`, revision 1;
no additional host exports or engine copy are required.

Open `/`, enter `cheat`, and press Enter to enable cheat functions. Commands:
`score set 1000`, `score add 100`, `score sub 100`, `speed 2`, `spawn`,
`sector 2`, `kill`, `win`. They retain the original playing/paused restrictions.

Default keys (editable under Options → Mods → Debug Utilities):

| Key | Action |
| --- | --- |
| R | Suicide (also available without cheat mode) |
| I / O / P | Paper / wood / stone player ball |
| F1 / F2 | Hold to apply upward / downward force |
| Backspace | Reset current sector's ball and modules |
| L | Add a life |
| Left Ctrl | Hold for 3× ball force, release for 1× |
| F | Hold to skip scene rendering |
| J / K / N / M | Hold to place paper / wood / stone auxiliary ball / box; release to physicalize |
| Arrows; right Shift / Ctrl | Move auxiliary preview horizontally; up / down |

The browser or OS may reserve a key; remap it in the mod settings if necessary.
`speed` changes ball force, not the fixed simulation/input tick rate.

## Standalone build

Clone https://github.com/gzz2000/Ballanced (branch `web-port`) for `sdk/wasm`,
and https://github.com/gzz2000/BallanceModLoaderPlus (branch `web-port`) for this
source. With Emscripten 6.0.13 activated:

```sh
emcmake cmake -S mods/DebugUtilities -B build/DebugUtilities-web -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DBALLANCE_WEB_SDK=/absolute/path/to/Ballanced/sdk/wasm
cmake --build build/DebugUtilities-web
```

Output: `build/DebugUtilities-web/DebugUtilities.bwmod`, including the MIT license.
The existing native `ballance_add_mod` build remains available. Native platform
compilation is not part of this browser qualification.

The port adapts CK array-string signatures, explicit integer timer overloads,
portable camera resource paths and number parsing using existing ABI functions.
Temporary object ownership is rediscovered from the live PH array after a world
restore, rather than retained as stale pointers/row numbers. Idle DebugUtilities
no longer continually overwrites other mods' render options.

## Snapshot limits

PracticeTools owns engine/world snapshots, not this plugin's private C++ members.
Completed summon operations can be covered by world snapshots. Do not assume
that an in-progress placement preview, deferred transformation/reset callback,
cooldown or private speed toggle is rewound. Snapshot integration for all such
transient plugin state is not implemented by this port. Changing force, sectors
or objects during TAS playback deliberately changes the recorded trajectory.
The read-only WASM diagnostic `BML_WebDebugUtilitiesStatus` exists for automated
qualification; it is not a stable third-party SDK API.
