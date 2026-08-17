# Block-Hit Sound (Client Heuristic)

OVson can play a block-hit sound when it estimates that the local player
blocked a player hit with a sword. The feature and its diagnostic logging are
disabled by default. Enable it in the Utils section of either ClickGUI layout;
the debug sub-setting logs structured accept/reject reasons through OVson's
normal logger.

## Sound controls

`Default` remains the initial source and uses Minecraft's built-in
`random.anvil_land` sound with its existing pitch. `Custom` uses the selected
WAV from:

```text
%APPDATA%\OVson\sounds
```

Use **Open sounds folder** (shown as **Folder** in layout A) to create and open
that directory, then copy one or more supported WAV files into it. **Next WAV**
(**Next** in layout A) cycles alphabetically through the WAV filenames in the
folder and shows the selected short filename. **Reload WAV** validates and
recaches the selected file after it has been replaced. **Preview** plays the
current source at the current volume without waiting for the heuristic and
works even while the block-hit module is disabled.

The volume slider is persisted from 0% through 100%. Its default is 22%, which
matches the previous Minecraft playback volume. Zero is silent. The value is
applied only to this sound: Minecraft's global master volume is not changed.
It controls both the vanilla sound and the DirectSound custom buffer.

Custom WAV support is deliberately narrow and predictable:

- RIFF/WAVE with uncompressed integer PCM format tag 1
- 16-bit samples
- mono or stereo
- sample rates from 8,000 Hz through 192,000 Hz
- at most 8 MiB per file and at most 15 seconds of audio

Compressed WAV, floating-point WAV, 8/24/32-bit PCM, MP3, and OGG are not
supported. Empty, malformed, truncated, oversized, over-duration, unreadable,
or missing files are rejected safely. OVson shows one concise notification for
each load/reload failure and falls back to the default Minecraft sound while
retaining the selected filename. It does not retry or log the failure on every
hit.

WAV reading, validation, DirectSound initialization, and buffer replacement
run on one owned worker thread. A validated sound is cached in a DirectSound
secondary buffer, so a hit only enqueues playback and never reads from disk.
The worker is joined and the buffer/device are released during unload.

This is not exact server confirmation. An unmodified Minecraft 1.8.9 server
sends a generic entity-hurt status but does not serialize whether sword
blocking reduced that damage. OVson therefore correlates that server-originated
hurt status with a nearby remote-player swing, local sword-blocking state, and
server health-loss or velocity evidence. A short close-range fallback covers
absorption and packet reordering. Team, player/alive/spectator, range, timing,
and environmental evidence reduce false positives.

Choosing a custom sound or using Preview does not change this detector and does
not make heuristic detection server-confirmed.

Known ambiguity remains when unrelated damage, knockback, and another player's
swing happen in the same short window. Fire, lava, and water lower confidence
rather than suppressing every melee hit; falling, suffocation, void contact,
and a nearby-in-time explosion packet are stronger vetoes. Modified servers or
clients that suppress, rename, or transform the observed packets can produce
false negatives. Poison, wither, starvation, cactus, and plugin-generated
damage have no dependable cause packet here, so a coincident nearby swing can
remain ambiguous. In-game validation is still required for Vanilla, Forge,
Lunar, Badlion, high latency, absorption, and modified-knockback servers.

## Development build

The core DLL and deterministic tests do not require Slint. From a Visual Studio
x64 developer prompt:

```powershell
cmake -S OVson -B build-dll -G Ninja `
  -DOVSON_BUILD_LOADER=OFF -DOVSON_BUILD_TESTS=ON `
  -DCMAKE_BUILD_TYPE=Release
cmake --build build-dll --parallel
ctest --test-dir build-dll --output-on-failure
```

The build needs a JDK whose `javac` supports `--release 8`; CMake generates the
embedded plugin API jar in the binary tree. Regenerate the packet-hook byte
header after changing `PacketFilterHook.java`:

```powershell
OVson\OVson\tools\GeneratePacketFilterHookHeader.ps1
```

Full builds include the Slint loader by default. Install Slint C++ 1.16.1 and
set `Slint_DIR` or `SLINT_INSTALL_ROOT` when it is not in the default location.
