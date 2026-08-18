# Change report

Fork: `Boboalover/ovson-blockhit` -> `alperenproo/ovson`

Two features (block-hit sound, Bedwars tools) plus fixes to shared client
code. The shared-code changes are listed first because they affect the whole
client, not just the new features, and are the ones worth reviewing closely.

---

## 1. Shared client code (outside Bedwars)

### `Render/RenderHook.cpp`, `Render/RenderHook.h` -- uninject no longer breaks other injected mods

**Problem.** Players commonly run more than one injected DLL. Teardown
undid our hooks unconditionally, which is only safe when we are still the
outermost hook:

- `SetWindowLongPtr(hwnd, GWLP_WNDPROC, originalWndProc)` -- if another mod
  subclassed after us, the window's current proc is theirs and their saved
  "previous" pointer is our `hookedWndProc`. Restoring dropped them out of
  the chain and their input handling died silently.
- `MH_DisableHook(MH_ALL_HOOKS)` -- MinHook rewrites the function's original
  prologue bytes, erasing a later hooker's jump. `MH_Uninitialize` then frees
  the trampoline their chain still points at.
- `FreeLibraryAndExitThread` ran even on the pre-existing "threads did not
  drain, skipping MinHook free" path, unmapping code other modules were
  still calling.

**Change.** After `MH_EnableHook` succeeds we snapshot the first 16 bytes of
each hooked target. At teardown we only unhook when those bytes still match
and when the window's current WndProc is still ours. If either check fails we
leave everything installed, `g_unloading` turns the stubs into pure
pass-throughs, and `RenderHook::mustStayLoaded()` reports that the module has
to stay mapped. New public API: `bool RenderHook::mustStayLoaded()`.

Also moved the `g_unloading` early-out above `Watchdog::tickFrame()` so a
stub that may remain installed indefinitely touches no subsystem after
shutdown.

### `dllmain.cpp`

- Honours `RenderHook::mustStayLoaded()`: `ExitThread(0)` instead of
  `FreeLibraryAndExitThread`, so a leaked module costs a few hundred KB
  rather than taking another mod down. Calls `Config::saveNow()` on that path
  because `DLL_PROCESS_DETACH` no longer runs.
- Removed the `BedDefenseManager` and `TextureLoader` includes and calls
  (feature deleted, see section 4).

### `Logic/PacketHook.cpp`

`PacketHook::update()` ran the block-hit correlator *after* the
`if (s_injected) return;` early-out. Discovery is a one-shot job, the
correlator is not: it converts queued server signals into sounds for the
whole session, and those signals only start arriving once injection
succeeds. Moved the tick above the guard. Discovery below it stays throttled.

Also hosts the block-hit JNI export
`Java_net_ovson_api_hook_PacketFilterHook_onServerPacket`.

### `JavaHook/JavaHook.cpp`

- Kept: `shutdown()` now calls `DisposeEnvironment()`. A JVMTI environment
  obtained with `GetEnv` stays registered after the DLL unloads; on
  re-injection into the same JVM the old environment still holds its
  capabilities, and the fresh `AddCapabilities` call can then be refused.
- Removed: the placement-hook capability request (`can_generate_breakpoint_events`
  and friends). HotSpot returned `JVMTI_ERROR_NOT_AVAILABLE` (98) for it in
  every session tested, so the feature it backed never worked. Note
  `AddCapabilities` is all-or-nothing, so that request also denied the
  capabilities that *were* available.

### `Utils/StbImageImpl.cpp` (new)

`stb_image` is header-only and needs exactly one TU to define
`STB_IMAGE_IMPLEMENTATION`. That TU used to be `Render/TextureLoader.cpp`,
which existed only for bed-defense block textures. `ClickGUI/Input.cpp`
decodes downloaded PNGs and still needs the symbols, so the definition now
lives in a file with no feature attached to it. Deliberately kept out of the
`/W4` list -- vendored code, not clean at that level.

### `Config/Config.h`, `Config/Config.cpp`, `Utils/Logger.cpp`

Removed `isBedDefenseEnabled` / `setBedDefenseEnabled` and the
`DebugCategory::BedDetection` and `DebugCategory::BedDefense` enumerators,
with their serialization and log-category cases.

### `Chat/Commands.cpp`

Removed `.bedplates` and `.bedscan`. `.lookat` kept, minus the block
name/metadata lookup that went through the deleted manager. Bedwars *stats*
code (`stats.bedwarsWins`, `bedwarsStar`, `.numdenicker beds`,
`/play bedwars_*`) untouched -- unrelated to the removed feature.

### `ClickGUI/Render.cpp`, `ClickGUI/Tabs/Utils.cpp`, `ClickGUI/Tabs/Debug.cpp`

Removed the Bed Defense module entry, its two debug sub-toggles, its card in
the Utils tab, and the map-height override module. Added the Bedwars "Alert
Output" choice to the legacy layout for parity with the new tab.

---

## 2. Block-hit sound (new)

| File | Role |
| --- | --- |
| `Logic/BlockHitSound.cpp/.h` | Session state, JNI reads, signal queue, boundary resets |
| `Logic/BlockHitHeuristic.cpp/.h` | Pure correlator: swing/hurt/health/velocity windows, hazard vetoes |
| `Logic/BlockHitAudio.cpp/.h` | Sound source selection, custom `.wav` discovery |
| `Logic/BlockHitAudioBackend.cpp/.h` | DirectSound playback worker |
| `Logic/PacketFilterHook.java`, `Logic/PacketFilterHook_bytes.h` | Netty handler and its compiled bytes |
| `tools/GeneratePacketFilterHookHeader.ps1` | Regenerates the header from the `.java` |

Detection is entirely packet-driven, so the client never guesses from local
state. All decision logic is in `BlockHitHeuristic`, which has no engine
dependency and is unit tested.

Links `Dsound.lib` and `Dxguid.lib`.

Diagnostics (debug-gated): per-decision reasons, and a 2-second telemetry
line counting arrivals per signal kind next to the local player's
blocking/sword state, so a silent feature can be diagnosed from a log rather
than guesswork.

---

## 3. Bedwars tools (new)

| File | Role |
| --- | --- |
| `Logic/Bedwars/BedwarsCore.cpp/.h` | Pure logic: lifecycle, chat/scoreboard parsing, alert rules, teams, resources |
| `Logic/Bedwars/BedwarsRuntime.cpp/.h` | JNI scanning, notification routing, snapshot for the renderer |
| `Logic/Bedwars/BedwarsConfig.cpp/.h` | Settings, own format version, own file |
| `Logic/Bedwars/BedwarsMaps.cpp` | Built-in per-map build limits |
| `Render/BedwarsOverlay.cpp/.h` | Draggable HUD panels |
| `ClickGUI/Tabs/Bedwars.cpp` | Settings tab |

`BedwarsCore` is deliberately engine-free so the rules are testable.

Notable alert-system decisions worth a look:

- **Alert cooldown is keyed on the event, not the category.** Keyed on
  category alone, two different alerts of the same category within the
  cooldown collapsed into one -- a team revealing Sharpness and Protection
  together only ever reported one of them.
- **Enemy Protection is inferred from armour glint.** Hypixel only announces
  a team upgrade to the buying team; the glint on their armour is the only
  read a client gets. Tracked per team, so the first wearer reveals it and
  the rest stay quiet.
- **Per-item dedup is time-based, not once-per-match.** An item re-alerts
  only after the player has been seen holding something else and enough time
  has passed, so a re-bought pearl reports while an item held continuously
  never repeats.
- **Death clears inventory state but keeps armour.** Armour survives death in
  Bedwars, so wiping the armour baseline re-announced the same armour after
  every kill.

---

## 4. Removed

`Logic/BedDefense/` (manager + block hook), `Render/DefenseRenderer.*`,
`Render/TextureLoader.*`, `JavaHook/BedwarsPlacementHook.*`, and the
`AntiMisplace` / `BedTracker` modules with their HUD panels and settings.

The placement hook needed JVMTI capabilities HotSpot only grants during the
OnLoad phase, which a DLL injected into a running client cannot reach, so it
never functioned. The rest was removed with it at the fork owner's request.

---

## 5. Build and tests

`OVson/OVson/CMakeLists.txt`: new sources for both features, `Dsound.lib` and
`Dxguid.lib`, `Utils/StbImageImpl.cpp`, three test executables, and removal of
the deleted units. Root `CMakeLists.txt` exposes `OVSON_BUILD_TESTS`
(default ON).

Tests: `OVsonBedwarsTests`, `OVsonBlockHitHeuristicTests`,
`OVsonBlockHitAudioTests`, registered with `ctest`. They cover pure logic
only -- no JNI, no rendering -- and are quick enough to run on every build.

## 6. Known gaps

- Other players' inventories are not visible on 1.8.9; only the held item and
  worn armour are. An item an enemy owns but never holds cannot be reported.
- Enemy Sharpness detection needs them to be holding the sword at the moment
  they are scanned; Protection, being armour, is the steadier of the two.
- `Module::ShopHelper` is present but reports itself unavailable: it has no
  safe shop-container hook yet.
