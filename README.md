# Pocket Euclidean

Pocket Euclidean is an original C++17 groovebox project for desktop systems
and retro handhelds. It targets an eight-track Euclidean sequencer with a WAV
sampler and replaceable lightweight synthesizer voices. TrimUI Brick Pro /
NextUI / PortMaster is the first handheld target under consideration; other
Linux handhelds and desktop systems remain part of the portability design.

## Project status

The repository currently provides a portable sequencer and modulation core,
sampler/synth rendering path, mixer DSP, fixed-size audio/UI event queues,
logical UI state/input handling, and platform interface contracts. It is not
yet a complete playable application: SDL2 device/rendering integration,
project/settings persistence, effects beyond the current low-pass filter, and
device packaging are not complete.

Build and test status is deliberately conservative. C++ translation units
have been checked with strict warnings using an ARM bare-metal compiler, but
the native CMake build and CTest suites have not yet been run. No desktop or
handheld runtime is claimed as verified. The current Brick Pro ABI, compiler,
sysroot, SDL2 runtime, controller mapping, audio behavior, storage, and
PortMaster/NextUI compatibility all require target validation.

## Getting started

Use a desktop C++17 compiler and CMake:

```sh
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

Builds should use a separate build directory per compiler, target ABI, sysroot,
and feature configuration. The ARM toolchain files are templates only; they
do not include a compiler SDK, sysroot, or target libraries. In particular,
`cmake/toolchains/linux-armhf.cmake` is for 32-bit ARM hard-float Linux and is
not the Brick Pro target. Brick Pro is expected to use a compatible AArch64
userspace, subject to verification.

## Handoff guide

For work by another developer or coding agent, read the documents in this
order:

1. [docs/STATUS_AND_ROADMAP.md](docs/STATUS_AND_ROADMAP.md) — agreed scope,
   implementation status, verified/unverified items, and prioritized next
   work. Treat this as the current project handoff and status summary.
2. [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) — ownership boundaries,
   real-time constraints, audio parameter handoff, and core interfaces.
3. [docs/PLATFORMS.md](docs/PLATFORMS.md) — build families, target ABI and
   device validation gates.
4. [docs/UI_CONTROLS.md](docs/UI_CONTROLS.md) and
   [docs/UI_DESIGN_POLICY.md](docs/UI_DESIGN_POLICY.md) — logical controls,
   current UI behavior, and rendering requirements.
5. [docs/DECISIONS.md](docs/DECISIONS.md) — temporary defaults and unresolved
   product/engineering decisions.

Before changing code, check these constraints:

- Keep core code independent of SDL, operating-system APIs, and a particular
  handheld. Put platform integration behind the existing interfaces.
- The audio callback must not allocate, block, perform file I/O, or access UI
  state concurrently. Check the queue and event-transfer lock-free
  requirements before opening a device.
- `AudioParameterQueue` currently transfers track volume, pan, mute, Attack,
  Decay, filter enable/cutoff, and master volume at callback block boundaries.
  Rhythm, tempo, instrument assignment, modulation, and other sequencer
  mutations are not yet safe to make concurrently with playback.
- Keep buffers, voices, decoded samples, and queues bounded. Use the platform
  resource budgets as profiling gates, not as evidence of measured performance.
- Separate implementation, compilation, automated-test execution, and
  on-device validation in status reports. Never describe an unrun test or
  unverified device as successful.

## Build families

Native desktop builds use CMake and the host compiler. Linux handheld
cross-builds use a device-compatible compiler and sysroot with the templates
under `cmake/toolchains/`. The templates are starting points, not certified
toolchains; see [docs/PLATFORMS.md](docs/PLATFORMS.md) before configuring a
target build. A successful cross-build does not establish runtime
compatibility.

## Product and design references

- [docs/STATUS_AND_ROADMAP.md](docs/STATUS_AND_ROADMAP.md) is the canonical
  status/roadmap document.
- [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) describes state ownership and
  audio/UI/platform boundaries.
- [docs/UI_CONTROLS.md](docs/UI_CONTROLS.md) describes logical controls and
  testable UI behavior.
- [docs/UI_DESIGN_POLICY.md](docs/UI_DESIGN_POLICY.md) describes the visual
  language and renderer acceptance criteria.
- [docs/DECISIONS.md](docs/DECISIONS.md) records design defaults and open
  questions.
- [docs/PLATFORMS.md](docs/PLATFORMS.md) describes cross-build and hardware
  validation requirements.
