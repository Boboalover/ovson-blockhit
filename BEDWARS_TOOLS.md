# Bedwars tools + block-hit sound

Two features for Hypixel Bedwars on 1.8.9, plus the alert system that
backs them.

## Block-hit sound

Plays a sound the moment an opponent's hit lands on your raised sword.
1.8.9 never tells the client that a hit was blocked -- the server applies
the damage reduction silently -- so this correlates four server-originated
signals instead of guessing from local state:

| Signal | Packet | Used for |
| --- | --- | --- |
| Attacker swung | `S0BPacketAnimation` | who hit you, and how far away they were |
| You were hurt | `S19PacketEntityStatus` (status 2) | the hit itself |
| Health dropped | `S06PacketUpdateHealth` | confirmation the hit connected |
| You were knocked back | `S12PacketEntityVelocity` | confirmation the hit connected |

A sound is played only when a swing inside 5 blocks is followed by a hurt
event while you are blocking with a sword, and at least one of the two
confirmations lands in the matching time window. Fall, fire, lava, drowning,
suffocation, void and explosion damage are vetoed -- explosions using
`S27PacketExplosion` within 250 ms either side -- so environmental damage
taken while blocking stays silent.

Sound source is configurable: a built-in sound, or any `.wav` you drop in
the client's `sounds` folder. Volume, pitch and a preview button are in the
GUI.

## Bedwars tools

Everything below is off until the master switch is on, and each module is
independently toggleable.

**Alerts on enemies.** Armour tier upgrades; sword tier upgrades from stone
up; held items worth reacting to -- potions, bows, TNT, fireballs, ender
pearls, golden apples, magic milk, bridge eggs, water buckets, dream
defenders, knockback sticks; and consuming a potion, gapple or milk.

**Team upgrades.** Your own team's purchases are read from chat with their
tier. Enemy Sharpness is inferred from an enchanted sword and enemy
Protection from enchanted armour, since Hypixel only announces an upgrade to
the team that bought it.

**Alert output.** Overlay popups, `[OVSON]` chat lines, or both.

**HUD.** Event timers, build-limit height, resource totals and team upgrade
state, each an independently positioned draggable panel.

**Other.** Trap purchase and trigger notices with a refill reminder,
resource pickup tracking, and a per-map build limit table with a manual
override for maps the table does not know.

## Scope

Everything is read-only observation of information the client already has:
packet inspection, entity equipment, the scoreboard and chat. Nothing is
sent to the server, no input is synthesised, and no movement, aim or
timing is altered.

## Configuration

Bedwars settings live in their own file with their own format version and
are independent of the client's main config. Unknown keys are ignored on
read, so downgrading does not lose the rest of the file.

## Tests

`OVsonBedwarsTests`, `OVsonBlockHitHeuristicTests` and
`OVsonBlockHitAudioTests` cover the pure logic -- alert rules and dedup, the
block-hit correlator's timing windows and hazard vetoes, config round trips,
chat and scoreboard parsing. They build with the default `OVSON_BUILD_TESTS`
option and run under `ctest`.
