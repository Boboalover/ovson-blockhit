# OVson — change report for upstream

Fork: `Boboalover/ovson-blockhit` · base: `alperenproo/ovson`
Two features: **client-side block-hit sound** and **Bedwars Tools**.

**57 files changed, +12 280 / −2 999** (line-ending noise excluded — the fork's
working tree is CRLF, so diff with `--ignore-cr-at-eol` or the stat is useless).

Verified before writing this: all three deterministic test suites pass
(**68 + 56 + 66 = 190 checks**), the x64 DLL links clean, and every JNI change
was re-checked against a deobfuscated client. Details in
[Verification](#verification).

---

## 1. Read this first: the Lunar/Badlion mapping rule

Lunar Client ships Minecraft **deobfuscated**; Badlion ships it **obfuscated**.
`Java.h`'s lookup helpers try, in order:

1. deobfuscated name + deobfuscated signature
2. SRG name + deobfuscated signature
3. notch name + `notchSig ? notchSig : sig`

Step 3 is the trap: passing a notch *name* without a notch *signature* makes
JNI look up e.g. `f` with signature `Lnet/minecraft/client/multiplayer/WorldClient;`
on a jar where that type is called `bdb`. It silently returns null — no
exception, no log — and whatever depended on it just stops.

The fork adds a missing notch signature in **11 places**. Every one is an extra
trailing argument on an existing call. Steps 1 and 2 are untouched, so on a
deobfuscated client the first lookup still wins and **behaviour is bit-identical
to upstream**. The notch argument is only ever reached on a client where the
call was already failing.

Two call sites in `Render/BetterTab.cpp` still have the same latent gap. They
are outside this PR's scope and were deliberately left alone.

---

## 2. Non-Bedwars files (the ones worth your review)

### `Java.h` — +3 lines
Three entries added to the notch class-name map. Nothing removed, nothing
reordered:

```
net.minecraft.util.EnumFacing              -> cq
net.minecraft.entity.EntityLivingBase      -> pr
net.minecraft.client.entity.EntityOtherPlayerMP -> bev
```

### `SDK/McAccess.h` — the one real upstream bug fixed here
`theWorld` was declared with notch signature `Lavk;`. The correct type is
`Lbdb;`. `thePlayer` directly above it has its notch type right, which is why
on an obfuscated client the player read fine and the world always came back
null. Anything gated on "is there a world" silently no-oped.

Fixed to `Lbdb;`, plus a `FindFieldBySignature` fallback by type (deobf name
first, then `Lbdb;`) so a future remap does not reintroduce the same silent
null. **This is an upstream bug independent of either new feature** — worth
cherry-picking on its own.

### `Logic/ScoreboardTeams.cpp` — +23 / −4
- Four missing notch signatures: `inventory` → `Lwm;`, `armorInventory` →
  `[Lzx;`, `ItemStack.getItem` → `()Lzw;`, `ItemArmor.getColor` → `(Lzx;)I`.
- New fallback in `setTeamColorSticky`: `g_localTeam` previously came only from
  the single `You are on the X Team!` chat line Hypixel sends once per match.
  Inject mid-match and it is empty for the rest of the game. It is now also
  filled from the per-player team map when that map confidently resolves the
  local player's own name, and never overwrites an already-known value.

### `Chat/ChatSDK.cpp` / `Chat/ChatHook.cpp` — +6 / −2
Two missing notch signatures (`getChatComponent` → `()Leu;`,
`componentToJson` → `(Leu;)Ljava/lang/String;`), plus one call into the Bedwars
runtime from `processIncomingChat`.

### `Chat/Commands.cpp` — +27 / −66
`.bedplates` and `.bedscan` deleted with the bed feature. `.lookat` used to
print block name and metadata via `BedDefenseManager`; it now reads both
straight off the block state (`getUnlocalizedName`, `getMetaFromState`), so the
command's output is unchanged. The reporter is muted around the metadata lookup
so a client with different mappings cannot spam `FAILED:` into chat over a
cosmetic field.

### `JavaHook/JavaHook.cpp` — +16
`shutdown()` now calls `DisposeEnvironment()`. Each `GetEnv()` in
`initialize()` returns a *new* JVMTI environment that outlives the DLL unload.
Re-inject into the same running client and the zombie environment still holds
its granted capabilities, so the fresh `AddCapabilities()` can be refused for
no visible reason. Fifteen of those lines are the comment explaining it.

### `Render/RenderHook.cpp` / `.h` — +136 / −18
**Cross-mod-safe teardown.** People run several injected DLLs at once and hooks
stack. If another module hooked `wglSwapBuffers` after us, its trampoline holds
a copy of *our* jump. MinHook's disable path restores the function's original
prologue — deleting that module's jump — and `MH_Uninitialize` + `FreeLibrary`
then frees the trampoline its chain still points at. Same shape for the WndProc
chain.

Now:
- the post-install prologue bytes of each hooked target are snapshotted;
- teardown compares them before removing anything, and the WndProc restore is
  gated on `GetWindowLongPtr(...) == hookedWndProc`;
- if we are no longer outermost, everything stays installed and
  `RenderHook::mustStayLoaded()` returns true;
- `hookedSwapBuffers` checks `g_unloading` **before** `Watchdog::tickFrame()`,
  so once teardown starts the stub is a pure pass-through.

Also new: `gameWindowHandle()` (see §4), and the render-thread calls into the
Bedwars runtime/overlay.

### `dllmain.cpp` — +173 / −17
Honours `mustStayLoaded()`: saves config and `ExitThread` instead of
`FreeLibraryAndExitThread`, so the module leaks rather than taking another mod
down with it. Rest is the uninject work in §4.

### `Render/NotificationManager.*` — +59 / −24
New `addRich(...)` taking coloured segments, and a `maximumVisible` cap on both
entry points. Existing `add(...)` keeps its signature via defaults.

### `ClickGUI/Render.cpp` (+330/−38), `ClickGUI/Tabs/Utils.cpp` (+141/−37), `Tabs/Debug.cpp`, `Tabs/Tabs.h`
New Bedwars tab wiring, block-hit sound controls in Utils, and removal of the
two bed debug toggles.

### `Config/Config.*` — +121 / −46
Block-hit sound settings, `getDataDirectory()`, an opaque
`bedwarsSettingsData` blob, and removal of `BedDefense` / `BedDetection` from
`DebugCategory` (mirrored in `Utils/Logger.cpp`, −6).

### `CMakeLists.txt` — +222 / −30
Worth flagging: upstream's CMakeLists did not build. It listed
`Render/ClickGUI.cpp`, which does not exist, and omitted ~25 sources that do
(all of `ClickGUI/`, `JavaHook/`, `Plugins/`, several `Services/`, `Render/GL`,
`Shader`, `Framebuffer`, `BetterTab`, `Watchdog`, `CrashDump`,
`NumberDenicker`, `StatColors`). Presumably the real build is a `.vcxproj`.
The file list is now complete and three CTest targets were added.

### `Logic/StatsPoll.cpp`, `Logic/HypixelGameState.cpp`, `Logic/StatsTracker.internal.h`
Three one-liners: drop the bed-defense tick, forward scoreboard lines to the
Bedwars runtime, declare `closestTeamColor`.

---

## 3. Block-hit sound

New: `Logic/BlockHitSound.*`, `BlockHitHeuristic.*`, `BlockHitAudio.*`,
`BlockHitAudioBackend.*`, `Logic/PacketFilterHook.java` +
`PacketFilterHook_bytes.h`, `tools/GeneratePacketFilterHookHeader.ps1`.

It correlates a nearby player's swing with the server's hurt/health/velocity
packets while the local player is sword-blocking. **It is a heuristic** — 1.8
sends no confirmed block result — and the GUI says so.

### `Logic/PacketHook.cpp` — +344 / −58
- Correlator tick moved **above** the `if (s_injected) return;` early-out. It
  was below it, so once the pipeline hook was installed the correlator never
  ticked again and the feature was silent.
- Missing notch signatures on `getMinecraft` (`()Lave;`) and `theWorld`
  (`Lbdb;`), plus signature-based fallbacks.
- `reportStall(stage)` at each of the 8 early returns — one line per distinct
  stage, so a stalled discovery names itself instead of failing silently.
- **Structural NetworkManager discovery.** Every name-based lookup is a guess
  about how a client shipped the jar. As a last resort the fork walks
  `NetHandlerPlayClient`'s field hierarchy and picks the field whose object
  owns an `io.netty.channel.Channel` — Netty is third-party, so its type name
  survives any Minecraft remapping. Statics are skipped via
  `GetFieldModifiers`: `GetObjectField` on a static field is undefined and
  crashed the poll with an access violation on the first attempt at this.

### Java side
`PacketFilterHook` is defined once per JVM and **cannot be redefined**. After
changing it, Minecraft must be fully restarted — a re-inject hits
`LinkageError: duplicate class definition` and falls back to the stale class.

---

## 4. Uninject

Two independent problems, both in the *trigger*, not the teardown.

**Focus gate.** The hotkey only fired when `GetForegroundWindow()`'s PID equalled
ours. A launcher that hosts the Minecraft canvas inside its own frame owns that
window from a different process, so on such a client every press was dropped.
It now accepts the key when the foreground window is anywhere in the same window
tree as the surface RenderHook subclassed (`==`, `IsChild` either way, or same
`GA_ROOT`). Holding the key 3 s forces an uninject regardless — a guaranteed way
out when window ownership is unusual.

**Event namespace.** The DLL published `Local\OVsonUninject_<pid>` only, while
the loader probes `Local\`, ``, `Global\`. It now creates and waits on both.

Plus diagnostics: a one-shot line naming the hotkey state, VK code and both
event handles; a throttled line dumping the rejecting window's HWND/PID/class/
title; and a debug-gated 30 s heartbeat, because every other log line comes from
the render thread and a wedged poll loop was previously indistinguishable from
a missed key.

---

## 5. Bedwars Tools

New: `Logic/Bedwars/{BedwarsCore,BedwarsRuntime,BedwarsConfig,BedwarsMaps}.*`,
`Render/BedwarsOverlay.*`, `ClickGUI/Tabs/Bedwars.cpp`.

`BedwarsCore` is deliberately **free of JNI and Windows**: pure logic, which is
what makes the 68-check suite possible. `BedwarsRuntime` is the JNI/rendering
half.

Features: event timers, height limit, resource tracking, team/upgrade state,
and player alerts (armour tier, sword tier, Sharpness/Protection, potions,
knockback stick, TNT, fireball, ender pearl, golden apple, magic milk, bridge
egg, water bucket, dream defender).

Design points a reviewer might want:

- **Alert output** is `Overlay`, `Chat` (`[OVSON]` prefix) or `Both`.
- **Cooldowns are keyed by event, not by kind.** A single per-kind timestamp
  meant a player buying a stone sword and a golden apple in the same tick only
  produced one alert. Two different events of the same kind now no longer
  suppress each other.
- **Item dedup is a 45 s repeat window,** not permanent. Permanent dedup meant
  the second invisibility potion of a match was never announced.
- **Death clears inventory state but keeps armour.** In Bedwars a death costs
  the inventory; armour is a team upgrade and the player respawns wearing it.
  `forgetPlayerLoadout` clears held item, sword tier and item flags, and
  preserves both `armor` and its alert memory — otherwise the same armour line
  fires after every kill.
- **Potion metadata is masked with `& 0x0F`.** The raw value carries level II
  (`0x20`) and extended (`0x40`) bits, so an unmasked compare missed every
  Speed II.
- **Enemy Protection** is inferred from the armour's enchantment glint
  (`ItemStack.hasEffect()`), since the enchantment list is not readable from the
  client for other players.
- **Any sword tier above Wood alerts.** Stone is the first thing most players
  buy; treating it as uninteresting was the most-reported gap.

### Settings cleanup
`formatVersion` 4. Fifteen settings that nobody moved off their defaults are now
constants in `Configuration::Fixed`. Old keys still parse and are ignored; a
malformed or unknown key costs only its own field.

While fixing the round-trip test, two dual-source-of-truth bugs surfaced and
were removed: `resourceHud` and `timerX/Y/Scale` + `heightX/Y/Scale` duplicated
state that also lived in `hud[]`, and `deserialize` overwrote the duplicates
from `hud[]` at the end — so anything written to the duplicate alone was
silently dropped on the next load. `hud[]` is now the only home; the fields are
`hudLayout(HudId)` / `resourceHudVisible()` accessors.

---

## 6. Removed: bed features

Deleted entirely (8 files, −2 639):
`Logic/BedDefense/{BedDefenseManager,BlockHook}.{cpp,h}`,
`Render/{DefenseRenderer,TextureLoader}.{cpp,h}`.

Also gone: `Module::AntiMisplace`, `Module::BedTracker`, `HudId::BedDistance`,
`HudId::BedStatus`, the `.bedplates` / `.bedscan` commands, the Utils-tab card,
the two debug categories, and every bed-related config key (still parsed and
discarded for old files).

`Render/TextureLoader.cpp` was the single translation unit defining
`STB_IMAGE_IMPLEMENTATION`. `Utils/StbImageImpl.cpp` now owns it — it is
deliberately excluded from the `/W4` list, since `stb_image.h` is not
warning-clean.

---

## Verification

| Suite | Result |
|---|---|
| `OVson.Bedwars` | 68/68 |
| `OVson.BlockHitHeuristic` | 56/56 |
| `OVson.BlockHitAudio` | 66/66 |

Build: MSVC x64, exit code 0, `[13/13] Linking CXX shared library bin\OVson.dll`,
machine type `0x8664`. Only pre-existing warnings (`C4456`, `C4505`, `C4100`).

Confirmed in-game on an obfuscated client after the mapping fixes: world
resolved, lifecycle phase reached, all 12 JNI field/method bindings resolved,
and real alerts fired (sword upgrades, diamond armour, enemy Sharpness, golden
apple).

Three temporary diagnostic blocks are still in place and should be dropped
before merge if you would rather not carry them: `[Bedwars] held ...`,
`[Bedwars] JNI mapping`, `[Bedwars] lifecycle inputs` (all debug-gated), and
`[PacketHook] discovery stalled at: ...`.
