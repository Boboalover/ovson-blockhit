# OVson — change report for upstream

Fork: `Boboalover/ovson-blockhit` · base: `alperenproo/ovson`
Two fork features: **client-side block-hit sound** and **Bedwars Tools**.
The original upstream **Bed Defense** feature remains present; an accidental
fork-side deletion of it has been reversed without redesigning it.

**63 files changed, +13 656 / −272** against `alperenproo/ovson@main`
(line-ending noise excluded — the fork's working tree is CRLF, so diff with
`--ignore-cr-at-eol` or the stat is useless).

Verified before writing this: all three deterministic behavior suites pass
(**72 + 56 + 66 = 194 checks**), the 13-check Bed Defense restoration audit
passes, the x64 DLL links clean, and every JNI change was re-checked against a
deobfuscated client. Details in
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

### `Chat/Commands.cpp`
The original `.bedplates` and `.bedscan` commands are restored with their
upstream names, messages, Forge guard, manager calls, and registration.
`.lookat` remains the one mechanical compatibility exception: it reads block
name and metadata straight off the block state (`getUnlocalizedName`,
`getMetaFromState`) rather than routing through `BedDefenseManager`. Its output
is equivalent, and the reporter stays muted around the metadata lookup so a
client with different mappings cannot spam `FAILED:` into chat over a cosmetic
field.

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
Bedwars runtime/overlay. The original `DefenseRenderer::render` and
`BedDefenseManager::tick` calls are restored alongside those additions.

### `dllmain.cpp`
Honours `mustStayLoaded()`: saves config and `ExitThread` instead of
`FreeLibraryAndExitThread`, so the module leaks rather than taking another mod
down with it. The original `BedDefense::TextureLoader::setModule(...)`
initialization is restored after config initialization. The rest is the
uninject work in §4.

### `Render/NotificationManager.*` — +59 / −24
New `addRich(...)` taking coloured segments, and a `maximumVisible` cap on both
entry points. Existing `add(...)` keeps its signature via defaults.

### `ClickGUI/Render.cpp`, `ClickGUI/Tabs/Utils.cpp`, `Tabs/Debug.cpp`, `Tabs/Tabs.h`
New Bedwars tab wiring and block-hit sound controls in Utils. The original Bed
Defense card remains in the Utils tab and UTILS legacy window, and the original
Bed Detection / Bed Defense diagnostic controls are restored in both debug
layouts.

### `Config/Config.*`
Block-hit sound settings, `getDataDirectory()`, an opaque
`bedwarsSettingsData` blob, and the original independent `bedDefenseEnabled`
setting. Its upstream default (`false`), load fallback, save path, accessors,
Forge guard, and `BedDetection` / `BedDefense` debug categories are restored.
The logging categories are mirrored in `Utils/Logger.cpp`. Bedwars config
format version 5 is unchanged and does not control Bed Defense.

### `CMakeLists.txt` — +222 / −30
Worth flagging: upstream's CMakeLists did not build. It listed
`Render/ClickGUI.cpp`, which does not exist, and omitted ~25 sources that do
(all of `ClickGUI/`, `JavaHook/`, `Plugins/`, several `Services/`, `Render/GL`,
`Shader`, `Framebuffer`, `BetterTab`, `Watchdog`, `CrashDump`,
`NumberDenicker`, `StatColors`). Presumably the real build is a `.vcxproj`.
The file list is now complete. Three behavior CTest targets and one 13-check
Bed Defense restoration audit are registered.
The original four Bed Defense translation units and four headers are restored
to that complete manifest. `Render/TextureLoader.cpp` again owns the sole
`STB_IMAGE_IMPLEMENTATION`, so the temporary `Utils/StbImageImpl.cpp` created
when Bed Defense was deleted has been removed.

### `Logic/StatsPoll.cpp`, `Logic/HypixelGameState.cpp`, `Logic/StatsTracker.internal.h`
The original config-gated Bed Defense tick is restored in `StatsPoll`. The
fork's scoreboard forwarding to the Bedwars runtime and `closestTeamColor`
declaration remain unchanged.

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
what makes the 72-check suite possible. `BedwarsRuntime` is the JNI/rendering
half.

Features: event timers, height limit, resource tracking, team/upgrade state,
and player alerts (armour tier, sword tier, Sharpness/Protection, potions,
knockback stick, TNT, fireball, ender pearl, golden apple, magic milk, bridge
egg, water bucket, dream defender, gold/diamond pickaxe, obsidian).

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
- **Potions are classified by name *and* damage value.** The raw damage
  carries level II (`0x20`) and extended (`0x40`) bits, so it is masked with
  `& 0x0F`; an unmasked compare missed every Speed II. The name matters too:
  1.8's `ItemPotion` overrides `getUnlocalizedName(ItemStack)` to return an
  already-translated string and Hypixel renames the stacks anyway, so a damage
  value we failed to read used to lose a potion that says Speed on it.
- **Enemy Protection** is inferred from the armour's enchantment glint
  (`ItemStack.hasEffect()`), since the enchantment list is not readable from the
  client for other players — but **only from the chestplate and leggings**.
  The kit helmet glints from spawn and the Feather Falling upgrade enchants
  nothing but the boots, so counting all four slots made every player on the
  map read as having bought Protection. Protection applies to the whole set,
  so the two remaining slots still catch it.
- **Any sword tier above Wood alerts.** Stone is the first thing most players
  buy; treating it as uninteresting was the most-reported gap.
- **Alert colours are chosen from Minecraft's sixteen chat colours.** The item
  is its own `MessageSegment` and carries its own colour, so the overlay and
  the chat line render identically; a colour outside that palette would look
  right on the overlay and white in chat. A test fails if one is ever added.
  Items with no colour that obviously reads as "that item" stay body-coloured
  rather than wearing a meaningless one.
- **Every item has its own on/off switch**, persisted as `itemAlert_<key>` and
  grouped in a collapsible Item Alerts section. An absent key means enabled, so
  neither an older config nor a later-added item is ever silently muted.

### Settings cleanup
`formatVersion` 5. Fifteen settings that nobody moved off their defaults are now
constants in `Configuration::Fixed`. Old keys still parse and are ignored; a
malformed or unknown key costs only its own field.

While fixing the round-trip test, two dual-source-of-truth bugs surfaced and
were removed: `resourceHud` and `timerX/Y/Scale` + `heightX/Y/Scale` duplicated
state that also lived in `hud[]`, and `deserialize` overwrote the duplicates
from `hud[]` at the end — so anything written to the duplicate alone was
silently dropped on the next load. `hud[]` is now the only home; the fields are
`hudLayout(HudId)` / `resourceHudVisible()` accessors.

---

## 6. Restored: original upstream Bed Defense

The original upstream Bed Defense/Bed ESP was accidentally removed while the
experimental fork-side bed modules were being deleted. That deletion is now
reversed. These eight standalone files are restored byte-for-byte from
`upstream/main`:

- `Logic/BedDefense/{BedDefenseManager,BlockHook}.{cpp,h}`
- `Render/{DefenseRenderer,TextureLoader}.{cpp,h}`

The original 64 texture assets and resource entries were already intact and
remain byte-identical to upstream. The original config, two GUI layouts, two
debug controls, commands, DLL module-handle initialization, render call, and
both upstream manager-tick call sites are restored. The feature stays disabled
by default, is controlled only by `bedDefenseEnabled`, and is not gated by the
Bedwars Tools master switch.

No new Bedwars-specific ESP was introduced. `Module::AntiMisplace`,
`Module::BedTracker`, `HudId::BedDistance`, `HudId::BedStatus`,
`BedwarsPlacementHook`, the eight-cell placement heuristic, and the
experimental RGB/opacity/team-owned-bed controls remain removed. Block-hit and
Bedwars alert behavior were not changed by this restoration.

`Render/TextureLoader.cpp` again owns the one and only
`STB_IMAGE_IMPLEMENTATION`. The temporary `Utils/StbImageImpl.cpp` is removed
as a direct reversal of the earlier Bed Defense deletion.

---

## 7. OVsonLoader — +351 / −88

Ten files, none of it feature work: it is what makes the loader survive being
closed while an injection is in flight.

- **`slint_ui/shutdown_coordinator.{cpp,h}` (new).** A tiny, platform-free state
  machine over the nine teardown stages (`Running` → `Requested` →
  `EventLoopExited` → … → `Complete`). `request()` is idempotent and stages can
  only advance, so a second close request or an out-of-order stage cannot walk
  the sequence backwards. Being free of Win32 is what lets it be unit-tested —
  it is covered by the `OVson.Bedwars` target.
- **`injector.cpp` — cooperative cancellation.** The injector has several
  multi-second waits (up to 3 s polling for `OVsonAlive_<pid>`, 1 s after a
  uninject request). They now poll `g_stopping` and bail out, instead of
  keeping the process alive after the user has already closed the window.
- **`injector.cpp` — per-PID loader-hint events.** The loader publishes
  `Local\OVsonLoaderHint_<pid>` before injecting; the DLL consumes it in
  `init()` to tell "launched via OVsonLoader" from "someone injected the DLL
  by hand" and only shows the download nag in the second case. The handles are
  tracked in a mutex-guarded map and closed in `shutdownInjector()`.
- **`injector.cpp` — `uninjectPid` probes three namespaces** (`Local\`, ``,
  `Global\`) when opening `OVsonUninject_<pid>`. §4 is the DLL half of that.
- **`tray_icon.{cpp,h}`** — `beginShutdown()` removes the notification-area
  icon immediately, leaving subclass teardown to the normal UI-thread stage, so
  the icon does not linger after the window is gone.
- **`updater.{cpp,h}`, `main.cpp`, `CMakeLists.txt`** — cancellation plumbed
  through the update check, and the shutdown coordinator wired into the app's
  exit path.

---

## Verification

| Suite | Result |
|---|---|
| `OVson.Bedwars` | 72/72 |
| `OVson.BlockHitHeuristic` | 56/56 |
| `OVson.BlockHitAudio` | 66/66 |
| `OVson.BedDefenseRestoration` | 13/13 |

Build: MSVC x64, exit code 0, linked `bin\OVson.dll`, machine type `0x8664`.
Only pre-existing warnings were observed (`D9025`, `C4244`, `C4456`, `C4505`,
and `C4100`); none came from a restored Bed Defense source file. A targeted
MSVC `/analyze` pass over the four restored translation units also completed
without source or analyzer warnings.

Confirmed in-game on an obfuscated client after the mapping fixes: world
resolved, lifecycle phase reached, all 12 JNI field/method bindings resolved,
and real alerts fired (sword upgrades, diamond armour, enemy Sharpness, golden
apple).

Four temporary diagnostic blocks are still in place and should be dropped
before merge if you would rather not carry them: `[Bedwars] held ...`,
`[Bedwars] JNI mapping`, `[Bedwars] lifecycle inputs` (all debug-gated), and
`[PacketHook] discovery stalled at: ...`.
