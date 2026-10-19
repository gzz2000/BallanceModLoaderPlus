# Speedrun Shortcuts

Native BML+ port of SpeedrunShortcuts 0.2.3 for the macOS arm64 Ballanced
runtime. The implementation reproduces the behavior and configuration schema
of the original Win32 `.bmodp` without loading or embedding its x86 code.

The default shortcuts are:

- Left Option+C: toggle Cheat Mode.
- Left Option+S: set the spawn point while in Cheat Mode.
- Left Option+E twice within three seconds: restart the level.
- Left Option+W plus 1-9: jump to a sector while in Cheat Mode.

`BMMO.StrictMode` is retained for configuration compatibility. It has no effect
until a native BMMO client and its anti-cheat state API are available; offline
restart behavior is identical to the original Mod.
