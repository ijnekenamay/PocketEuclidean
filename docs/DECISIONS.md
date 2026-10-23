# Implementation defaults and open decisions

These defaults make the sequencer core testable without silently changing the
specified parameter ranges. They are implementation choices, not additional
product requirements.

| Decision | Initial default |
| --- | --- |
| Initial tempo | 120.0 BPM |
| Tempo range | 40–240 BPM |
| Clock subdivision | One sequencer tick per sixteenth note; tempo is quarter-note BPM |
| Euclidean orientation | Step zero uses the unrotated pattern's step zero; the pulse formula is `((step * fill) % length) < fill` |
| Offset direction | Positive offset rotates active steps toward higher step indices; offset is reduced modulo length for pattern generation while the configured 0–31 value is retained |
| Zero fill | Unsupported; valid fill is 1–length. Use mute for silence |
| Live rhythm edits | Immediate; changing length wraps the current position modulo the new length |
| Start/stop | START begins the shared clock at step 0 and synchronously resets all track positions; STOP halts the clock; the next START resets and begins again at step 0 |
| Per-track phase | Each track advances on every shared master tick and wraps only at its own length |
| LFO capacity | Four independent LFO slots per track in the data model; the initial UI exposes slot zero |
| LFO free-rate range | 0.01–1 Hz, sampled once per master 16th-note tick |
| LFO destinations | One destination (`Eleng`, `Efill`, or `Eoffs`) per LFO, with one signed amount |
| LFO synchronized rates | Straight, dotted, and triplet whole-, half-, quarter-, eighth-, and sixteenth-note cycles |
| LFO phase reset | START resets LFO phase along with the synchronized transport |
| LFO quantization | Add normalized LFO output times signed integer depth to the stored base value, then round to nearest integer and clamp |
| Modulated phase | When effective length changes, normalize only that track's step position modulo the new length; do not reset master transport |
| Sample-and-hold | Deterministic per-track/per-LFO xorshift sequence; advance once per completed LFO cycle, reset to a reproducible seed on START |
| Jitter rate | Sixteenth, eighth, quarter, half, or whole-note intervals aligned to master tick zero |
| Jitter shift range | Signed integer master ticks, wrapped at the track pattern boundary, maximum eight ticks |
| Jitter collision | Lowest source-step index wins; duplicate destinations are discarded |
| Jitter MIX order | Drop, then shift survivors, then insert only at source-inactive empty steps |
| Jitter randomness | Track-local xorshift stream with optional project seed, reset on START |
| Jitter timing resolution | Whole master ticks; sub-tick offsets require a future audio lookahead scheduler |
| Track volume mapping | Quadratic UI-percent to linear gain, followed by fixed `1/sqrt(8)` track summing headroom |
| Pan representation | Normalized float from -1.0 (left) through 0.0 (center) to +1.0 (right); equal-power for mono, balance for stereo |
| Gain smoothing | 5 ms exponential smoothing for track, pan, mute, and master gain changes |
| Mixer startup | Smoothed track and master gains start at silence and ramp to configured levels |
| Master output volume | Global system audio preference, not stored in individual projects |
| Peak meters | Atomic linear peak values published per audio block; track meter follows track gain/pan, master meter follows output limiting |
| Clipping protection | Fixed per-track summing headroom plus a soft limiter near full scale; master volume is independent |

## Example groovebox preset

This is an illustrative eight-track rhythm preset, not a required project
default. All offsets are zero; role names are descriptive only.

| Track | Example role | `Eleng` | `Efill` |
| --- | --- | ---: | ---: |
| 1 | Kick | 16 | 4 |
| 2 | Snare | 16 | 3 |
| 3 | Hi-hat | 12 | 5 |
| 4 | Percussion | 7 | 3 |
| 5 | Bass synth | 15 | 4 |
| 6 | Synth / accent | 8 | 5 |
| 7 | Texture | 11 | 2 |
| 8 | Accent | 5 | 2 |

`ExamplePresetRhythms` in `Sequencer.h` provides these rhythm configurations
for callers and tests. The sequencer does not load this example automatically.
At 120 BPM, each shared 16th-note tick is 125 ms; the full combination of the
example cycle lengths repeats after 18,480 ticks.

## Modulation and platform boundaries

The core supports up to four LFO slots per track and keeps waveform generation,
rate calculation, routing, and effective-parameter application separate. The
initial UI edits one LFO per track. It provides Sine, Triangle, Square, Saw Up,
Saw Down, and deterministic Sample-and-Hold waveforms. Contributions update
on each master 16th-note tick. SYNC divisions include straight, dotted, and
triplet values from whole through sixteenth notes; FREE remains 0.01–1 Hz
regardless of tempo. Sample-and-Hold updates once per completed LFO cycle.

Jitter is a separate event-processing stage after the effective Euclidean
pattern and before triggering. It preserves the stored pattern, uses a
track-specific deterministic random stream, and refreshes its event mask at
the selected master-clock interval. Shift offsets are signed whole ticks,
wrapped within the track pattern; collisions keep the lowest source-step
index. Mix uses one shared intensity and applies drop, shift, then insertion.

The mixer stores track volume, normalized pan, and mute in project data. Mono
signals use equal-power panning; stereo signals use balance panning. Smoothed
gains avoid discontinuities, and per-track summing headroom precedes the master
soft limiter. Master volume belongs to global system audio preferences, while
track and master peak meters are runtime-only atomically published values.

`ITransportClock`, `IAudioBackend`, `IInputBackend`, `IMidiBackend`, and
`ITriggerSink` define replaceable platform boundaries. They are interfaces
only; there is no MIDI, SDL, or native backend implementation yet.
`SystemSettings` and `ProjectData` have independent version numbers and data
models. `SystemCapabilities` can filter settings categories unsupported by the
current platform. Their serialization and settings migration are not
implemented.
The Project/Storage system-settings category is for global storage preferences
only; creating, selecting, saving, or loading musical projects remains a
separate project-management flow.

## Known unresolved decisions

- The SDL2 application, audio callback, sampler format/limits, synthesizer,
  effects topology, and project format still need implementation.
- `Sequencer::processFrames` schedules 16th-note ticks using sample rate and
  BPM, emitting a sample offset and the eight-track trigger result to a caller
  supplied callback. Each tick result includes its master-clock position.
  START resets clock phase and all track positions; STOP halts tick generation.
  The eventual audio callback should use a non-allocating callback; the
  sequencer itself does not allocate while processing a block.
- TrimUI Brick Pro / NextUI userspace ABI, system libraries, PortMaster
  launch conventions, and device-specific controls have not been verified
  against a device or a known working PortMaster package. See
  [PLATFORMS.md](PLATFORMS.md) for target-specific validation gates.
- The build plan covers native desktops and separate Linux ARM hard-float and
  AArch64 cross-builds. The checked-in toolchain files are templates only;
  compiler, sysroot, SDL2 runtime, and ABI must be selected for each device.
  See [PLATFORMS.md](PLATFORMS.md).
- Physical button mapping, project location/file browser, missing-sample
  recovery, default samples, audio sample rate/buffer/polyphony, and target
  performance measurements remain open.
- The supplied specification asks for all eight tracks and real-time audio,
  but does not define the desired behavior when a live edit changes a track's
  phase. This implementation uses the immediate-wrap rule above.
