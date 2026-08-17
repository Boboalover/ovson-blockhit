# OVson Bedwars Tools

Bedwars Tools is a bounded, opt-in runtime for player alerts, local resource
tracking, map height, timers, own-bed state, placement protection, and HUDs.
The master switch gates processing without erasing saved child preferences.
Every module and every HUD is off by default.

## Lifecycle and performance

The runtime requires a valid local world/player, Hypixel Bedwars state, an
active match, and no replay state. World, dimension, server, mode, match,
death, respawn, disconnect, and unload transitions reset transient state.
Player/item deduplication and team observations persist for the match and reset
at the next real session boundary.

Chat and scoreboard producers use a bounded 128-line queue. The game thread
drains it. Player state is sampled at most every 300 ms and local inventory at
most every 250 ms. The optional bed worker is single, cancellable, joined, and
publishes only into the generation that started it. No tick or render callback
performs disk or network I/O.

Player-alert range is configurable to 256 blocks, but Minecraft's loaded entity
set is the effective upper bound. A bed scan is limited to loaded chunks within
the configured range (maximum ten chunks from the player) and to non-empty
chunk sections. This remains a comparatively expensive background scan; use a
longer interval if a high render distance makes it noticeable.

## Conservative player alerts

The player scan rejects the local player, known teammates, dead/spectating
players, invalid identities, out-of-range players, and unavailable visibility
evidence required by the selected mode. Every emitted player alert includes a
team-colored player name; unknown team uses a neutral readable color.

The held-item classifier has no generic display-name fallback. Its complete
allowlist is:

- Iron Sword and Diamond Sword tier increases
- Bow
- Knockback Stick
- Speed Potion, Jump Potion, and Invisibility Potion
- TNT
- Fireball
- Ender Pearl
- Golden Apple
- Milk Bucket

Potion identity requires the 1.8.9 potion item type and supported metadata. A
knockback stick requires the stick item, enchantment evidence, and its expected
visible name. Arbitrary named or enchanted sticks are ignored. A visible
enchanted sword can establish team Sharpness once, but no level is inferred.
Armor enchantment never implies Protection.

The explicit non-alert set includes every wool color, clay, End Stone, wood,
planks, glass, obsidian merely being held, ladders, water, sponges, iron, gold,
diamonds, emeralds, pickaxes, axes, shears, compasses, menus, shop icons, empty
hands, other building blocks, and all unrecognized or merely custom-named
items.

The first observation is a baseline. Armor and sword tiers are monotonic.
Important items deduplicate by player identity and category for the match, so
slot changes, range exit/re-entry, and entity-ID changes do not repeat an alert.
Different allowlisted items may alert separately. When held-item and active-use
signals occur together, the stronger use alert replaces the duplicate holding
alert.

## Anti Misplace

Anti Misplace does not use a Windows mouse message. A dedicated native JVMTI
hook resolves Minecraft 1.8.9 `PlayerControllerMP.onPlayerRightClick`, inspects
its loaded bytecode, and installs a breakpoint immediately before construction
of the outgoing block-placement action. At that point the real player, world,
held stack, target block, clicked face, and block-interaction result are
available. A confirmed cancellation verifies the exact 1.8.9 caller frame,
clears its generic-use fallback flag, and forces the controller method to
return `false` before either the placement action or local item placement
occurs. This also avoids the handled-interaction swing packet.

The hook does not transform Java classes and does not generate or replace
packets. It is installed idempotently, cleared on shutdown, and fails open if
its mappings, bytecode boundary, locals, or JVMTI capabilities cannot be
verified. Right-clicks already handled by an interactable block pass through,
preserving shops and chests.

The protection requires exact obsidian item identity and a confidently known,
live own bed in the current world generation. The resulting cell follows
Minecraft's replaceability rule: a replaceable target is the placement cell;
otherwise the clicked-face offset is applied.

Only these eight first-shell cells are permitted:

- The two cells directly above the bed halves
- Two cells along each long side
- One cell beyond the head
- One cell beyond the foot

Both bed axes and reversed half ordering are normalized. Confirmed obsidian
outside those cells is cancelled, including far from the bed. Non-obsidian
items always pass through. A destroyed own bed disables cancellation.

The GUI reports one of `Disabled`, `Not in active Bedwars match`, `Placement
hook unavailable`, `Waiting for team`, `Waiting for own bed`, `Own bed
destroyed`, or `Ready`. Details include team, hook failure, generation, bed
coordinates/axis, confidence source, and the last decision. Unknown or
ambiguous evidence always fails open.

Own-bed confidence requires a valid two-block bed plus a single unambiguous
team color from nearby wool, stained glass, or stained clay. Conflicting colors,
multiple matching beds, an unknown team, stale generation, or missing chunks do
not produce a candidate. The existing worker is deliberately unavailable in a
Forge environment; the GUI reports that limitation instead of claiming the
feature is ready.

## Timers, height, teams, traps, and resources

Event Timer prefers the live scoreboard countdown. Its fallback schedule is
used only after OVson observes the pre-game-to-active transition. Injecting into
an already-running game leaves fallback timing unknown rather than pretending
the injection time was match start.

The Height HUD parses formatted `Map:` scoreboard lines locally. Map matching
normalizes case, whitespace, punctuation, apostrophes, and hyphens. The bundled
reviewed table has 192 entries and separately records player-feet ceiling and
maximum placement Y. Unknown maps stay unknown. A per-map manual placement-Y
override takes precedence, is clamped to 1-511, and can be reset in either GUI.

Specific upgrade levels come only from explicit chat-visible purchase messages.
Before such evidence, the Upgrade HUD displays `?`. Enchanted remote equipment
does not create Protection or level claims. Trap state is `Unknown`, `Queued`,
or explicitly `Missing`; reminders run only after explicit missing-state chat,
never merely because no purchase has been observed.

Resource Tracker and Pickup Alerts describe only positive changes to aggregate
local inventory counts for iron, gold, diamonds, and emeralds. The first scan is
a baseline, moving stacks between slots does not change totals, and container
transfers are suppressed when the available evidence is insufficient. Remote
pickup attribution and Ender Chest tracking are unavailable. Shop Helper is
also unavailable because OVson has no reliable shop-container draw/click hook;
it performs no clicks, purchases, or approximate purchase blocking.

## HUD, GUI, and diagnostics

Event Timer, Height, Resources, Team State, Bed Distance, and Bed Status HUDs
have independent visibility, normalized position, scale, and reset actions.
They default hidden. Layout Preview is explicit and allows dragging panels back
within screen bounds. Both GUI layouts use the same version-3 named settings,
preserve module meanings across old configurations, and show Anti Misplace's
live readiness.

Debug mode writes transition and rate-limited summaries to
`C:\Users\Bob\AppData\Local\OVson\logs`. Diagnostics include lifecycle,
server/mode/replay detection, team, player rejection counts, ignored-item and
deduplication counts, potion/stick/Sharpness evidence, bed confidence, hook
installation/failure, placement inputs/result/propagation, map source,
inventory validity, enabled modules/HUDs, and queue drops. It does not open a
console or log every frame.

## Configuration migration

Version-1 and version-2 strings are parsed by stable names. Removed or unknown
keys are ignored independently, including obsolete renderer colors, opacity,
range, and toggle fields. Malformed, duplicate, truncated, NaN, and infinite
values cannot shift module meanings or invalidate unrelated valid settings.

## Loader shutdown and build limitation

Quit remains an idempotent staged shutdown: new work is gated, the tray icon is
removed, the Show watcher is stopped, workers are cancelled/joined, the Slint
loop is exited, owned resources are released, and Minecraft is never
terminated. Full loader integration still requires Slint C++ 1.16.1. Without
that installed runtime, only the isolated shutdown coordinator can be built and
tested.

For DLL and deterministic tests:

```powershell
cmake -S OVson -B build-bedwars -G Ninja -DOVSON_BUILD_LOADER=OFF -DOVSON_BUILD_TESTS=ON
cmake --build build-bedwars --target OVson OVsonBedwarsTests
ctest --test-dir build-bedwars --output-on-failure
```

The deterministic suite validates pure lifecycle, item, team, resource,
geometry, map, notification, migration, bounded-queue, publication, and
shutdown logic. It cannot prove live JNI/JVMTI behavior, server formatting,
rendering, or client-specific compatibility. The produced DLL therefore
requires the documented in-game validation pass before any live-success claim.
