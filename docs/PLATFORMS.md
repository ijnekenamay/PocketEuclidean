# Desktop and handheld platform strategy

Pocket Euclidean keeps its sequencer and audio DSP portable and adds
platform-specific application packaging only where required. A handheld is a
separate build target, not a promise that one binary will run on every device.

## Primary handheld target: TrimUI Brick Pro / NextUI

The primary planned device is the TrimUI Brick Pro running NextUI and launching
the application through PortMaster.

| Baseline | Value |
| --- | --- |
| SoC / CPU class | Allwinner A133P, ARM64-capable Cortex-A53 class |
| RAM | 1 GB |
| Display design target | 1024 x 768 |
| Language / build | C++17 / CMake |
| UI and input | SDL2; controller-first, with keyboard support in desktop builds |
| Rendering | SDL2 2D renderer; no OpenGL dependency in the initial release |
| Audio | SDL2 audio callback; ALSA is the initial Linux backend candidate; 48 kHz is unconfirmed on-device |
| Handheld architecture | AArch64 Linux, subject to confirmation of the actual runtime ABI |

The exact NextUI version, PortMaster installation/runtime, userspace ABI,
compiler triple and flags, libc baseline, SDL2 linkage, audio backend, and
controller mapping have not been verified on the device. The existing
`linux-aarch64.cmake` is only a generic starting point; it is not a certified
Brick Pro toolchain. Select the compiler, sysroot, and SDL2 headers/libraries
from a compatible PortMaster build environment. Never substitute host SDL2
libraries or infer ABI compatibility from the AArch64 CPU alone.

Use SDL2's audio API at the application boundary rather than binding the
sequencer or mixer directly to ALSA. For Linux handhelds, ALSA is the initial
SDL2 audio-backend candidate, not a verified Brick Pro / NextUI guarantee.
Confirm that the target SDL2 build enables the intended backend, identify the
runtime audio device, and measure accepted sample rates, buffer sizes,
latency, and stability on the exact target image. Keep the audio engine behind
the replaceable audio-backend interface so another SDL2 backend or platform
API can be selected for other systems.

### Brick Pro resource budget and performance gates

The Brick Pro has 1 GB of physical RAM shared with NextUI and its services; do
not treat all of it as available to the port. The following are conservative
engineering targets for the first playable build, not measured device
capabilities. Keep them configurable and revise only from documented
measurements on the exact target image.

| Resource | Initial target / limit | Validation |
| --- | --- | --- |
| Application resident memory | Aim for at most 128 MiB in a representative session, including decoded samples | Measure process peak RSS during load, playback, project changes, and prolonged use; verify no memory pressure or OOM termination |
| Decoded sample cache | At most 64 MiB by default; reject or evict samples explicitly rather than silently exceeding the budget | Test cache accounting, exhaustion behavior, and project/sample switching |
| Simultaneous audio voices | Start with a fixed cap of 32 total voices across all tracks; voice stealing must be deterministic | Stress all tracks with overlapping triggers and record CPU, latency, and steal counts |
| Audio callback budget | Complete every callback before its buffer deadline; target p99 processing below 50% of the callback period and keep worst-case processing below 80% under the agreed stress test | Measure on-device with the actual SDL2/ALSA configuration; no logging, allocation, blocking locks, or file I/O in the callback |
| 48 kHz / 256 frames | Candidate only: one buffer period is about 5.33 ms | Query the obtained SDL audio spec; use it for deadline calculations rather than assuming the requested spec was granted |
| Handheld rendering cadence | Target a stable 30 frames/s for the simple 2D UI; audio has priority over rendering | Measure worst-case frame time while all eight patterns and meters update; reduce render cadence before risking audio deadlines |
| Main-screen command count | At most 267 fixed-size commands (background, two status lines, eight labels, and 256 steps) | Keep the command list allocation-free and validate its capacity at compile time |

The initial memory and timing limits are product budget candidates, not
measured target capabilities. The current code provides a fixed 32-voice
sampler pool, a caller-owned sample cache with a 64 MiB cap, and preallocated
audio buffers; the 128 MiB process RSS target and callback timing goals are not
enforced or measured. Keep resource handling bounded, fail explicitly on
capacity exhaustion, and avoid duplicating decoded samples between project,
instrument, and audio layers.

Use simple SDL2 2D primitives, cached or bitmap text, bounded buffers, and
allocation-free steady-state rendering/audio. Do not add blur, bloom,
full-screen post-processing, unnecessary per-frame allocations, or unbounded
queues. Any future queue between audio and UI must have a fixed capacity and a
defined overflow policy; audio must never wait for rendering. Profile mixer
math and step rendering on the A133P before increasing polyphony, sample rate,
frame rate, or effect complexity. A missed performance target should first
reduce optional work or load, not compromise musical timing.

Keep application logic, input mapping, rendering, audio, storage, and package
integration as separate layers. Prefer static linking for project-owned code
where practical; use target-compatible SDL2/system libraries from the selected
environment or explicitly package compatible runtime libraries. Audit final
shared-library dependencies and licenses. Do not add OpenGL, Vulkan, X11, or
Wayland dependencies to the initial target.

### PortMaster package requirements

Follow the [official PortMaster packaging guide](https://portmaster.games/packaging.html)
at release time. Its current structure requires a lowercase port directory
name, a capitalized `.sh` launch script, `port.json`, `README.md`,
`screenshot.png`, `gameinfo.xml`, and a matching port-data directory; include
the used source, library, and asset license files under a licenses directory.
The screenshot must show the port's main function and be at least 640 x 480
with a 4:3 aspect ratio. The `port.json` architecture must match the released
binary.

The launch script must use the PortMaster-provided control environment rather
than assuming a fixed installation path: locate and source `control.txt`,
load the appropriate controls, set the package working directory and
persistent configuration path, capture diagnostics, configure any
package-local libraries, launch through the expected PortMaster helpers, and
call `pm_finish` after the application exits. Follow the official example for
the selected runtime and port type; do not invent a device-specific shortcut
or bundled system-library set.

No package, launcher, screenshot, or target binary is release-ready yet: the
application, SDL2 shell, persistent storage, and tested target runtime do not
exist in this repository.

### NextUI validation gate

NextUI's PortMaster integration is community-provided. Do not claim NextUI
support until a concrete NextUI and PortMaster version pair has passed these
checks on the Brick Pro:

1. Confirm PortMaster installation, runtime files, and expected environment.
2. Inspect the executable ABI and all linked libraries on the target.
3. Initialize the display and verify SDL2 controller mappings.
4. Test audio output and clean audio-device shutdown.
5. Save and reload data on the actual SD-card filesystem.
6. Exit through the launcher and confirm clean return to NextUI.
7. Record sleep/resume, menu shortcuts, and system-button limitations.

Record the exact tested versions and results for every release. A successful
AArch64 cross-build is not runtime compatibility evidence.

## Planned build families

| Build family | Expected build approach | Device-specific checks |
| --- | --- | --- |
| Windows, Linux, macOS desktop | Native CMake build | SDL2 development package, audio device, controller mapping |
| Linux ARM hard-float (32-bit) handhelds | `linux-armhf.cmake` with the device-compatible SDK/sysroot | Userspace ABI, CPU/ABI flags, SDL2 and PortMaster runtime libraries |
| TrimUI Brick Pro / NextUI | `linux-aarch64.cmake` with a PortMaster-compatible compiler and sysroot | Exact ABI, NextUI/PortMaster versions, SDL2, inputs, audio, storage, and exit must be tested on hardware |
| Other Linux AArch64 handhelds | `linux-aarch64.cmake` with the device-compatible SDK/sysroot | Confirm the OS userspace is 64-bit; CPU capability alone is insufficient |
| Other Linux handheld targets | Native or project/vendor toolchain file | Distribution libc, ABI, SDL2 version, input and audio backends |
| Non-Linux handheld platforms | Future platform-specific port | SDL2 availability, packaging, lifecycle, controls, and permitted APIs |

The ARM toolchain files are starting points, not bundled SDKs. Install or
locate a compiler and target sysroot compatible with the device OS. SDL2 and
other target libraries must come from that same target environment. Do not use
desktop headers or libraries to produce a handheld binary.

## Build examples

Desktop build:

```sh
cmake -S . -B build/desktop
cmake --build build/desktop
ctest --test-dir build/desktop --output-on-failure
```

32-bit ARM Linux build (provide the actual SDK/sysroot paths for the target):

```sh
cmake -S . -B build/armhf \
  -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/linux-armhf.cmake \
  -DPOCKET_SYSROOT=/path/to/target/sysroot \
  -DCMAKE_C_COMPILER=/path/to/sdk/bin/arm-linux-gnueabihf-gcc \
  -DCMAKE_CXX_COMPILER=/path/to/sdk/bin/arm-linux-gnueabihf-g++
cmake --build build/armhf
```

The checked-in `cmake/toolchains/linux-armhf.cmake` defaults to
`arm-linux-gnueabihf-gcc` / `arm-linux-gnueabihf-g++`; override these cache
entries when the target SDK uses different compiler paths. This is a
32-bit ARM hard-float configuration and must not be used for an AArch64-only
userspace such as the expected Brick Pro target.

64-bit ARM Linux follows the same pattern with
`cmake/toolchains/linux-aarch64.cmake` and a target-compatible AArch64
compiler/sysroot. For Brick Pro, obtain these from the selected PortMaster
build environment; the compiler name in the template is not a promise of
runtime compatibility.
When tests are cross-compiled, run them with a configured
`CMAKE_CROSSCOMPILING_EMULATOR` or on the actual target; a successful build
alone does not verify runtime behavior.

Use a separate build directory for each compiler, ABI, sysroot, and relevant
feature configuration. Never reuse a CMake cache across different target
devices.

## Compatibility policy

- Keep core sequencing and DSP code independent of SDL, OS APIs, and device
  controls.
- Use CMake toolchain files and target SDKs rather than hard-coding one
  handheld's compiler flags into common targets.
- Add platform-specific input, launch scripts, and packaging outside the core.
- Verify the target userspace ABI and SDL2 runtime for every release target.
- Keep per-device button mappings configurable; physical button names and
  mappings are not consistent across handhelds.
- Translate keyboard, SDL GameController, and any joystick fallback into the
  shared logical actions documented in [UI_CONTROLS.md](UI_CONTROLS.md).
- Keep audio, input, and MIDI APIs behind replaceable platform interfaces;
  never couple the sequencer to a specific OS or MIDI library.
- Use the common versioned system-settings model while exposing only the
  settings that a concrete platform can support. Store system preferences
  separately from versioned musical project data.
- Record build, input, audio, save-path, and exit validation per actual device.
- Keep a release record of the tested NextUI and PortMaster versions, SDL2
  linkage, ABI, and the hardware test results listed above.
- Treat the Brick Pro resource budgets above as explicit profiling gates; do
  not raise voice, memory, or rendering budgets without measurements and a
  regression stress test on the target.
- Do not claim a device is supported merely because its CPU architecture
  matches a cross-compiled binary.

The repository also includes a platform-independent `AudioCallbackProcessor`,
sampler/synth renderer, mixer DSP, bounded trigger-event and parameter queues,
and UI state/input logic. No SDL2 application, SDL audio-device adapter,
rendered UI, target library package, device input profile, persistence layer,
or hardware compatibility validation is available yet.

## Official packaging reference

PortMaster package structure and metadata requirements can change; verify them
against the [official packaging guide](https://portmaster.games/packaging.html)
when preparing a release. PortMaster's
[development guide](https://portmaster.games/porting.html) recommends using
its compatible build environments and testing on the actual device. These
guides are release references, not evidence that NextUI/Brick Pro support has
already been validated.
