# Pocket Euclidean — 拡張性アーキテクチャ

This document describes the current code boundaries, the musical-state contract,
and where future platform implementations belong. The interfaces in this
document are contracts only; no SDL, audio-device, or MIDI backend is shipped
yet.

## Layer boundaries

```text
Physical keys / SDL GameController / joystick
        └── platform input adapter ──> logical ui::ActionEvent
                                             └── ui::UiController / UiState

SystemSettings (versioned, global)       ProjectData (versioned, musical)
        └── platform settings store             └── project store

ITransportClock <── Sequencer ──> TickResult ──> ITriggerSink / audio engine
                                     │
                                     └── per-track LFO ──> effective rhythm
                                            │
                                      Euclidean pattern
                                            │
                                      JitterEngine
                                            └──> trigger events

Track audio buffers ──> audio::Mixer ──> stereo output
                              │
                              └──> atomic PeakMeters ──> UI reads

IAudioBackend / IMidiBackend are replaceable platform ports;
the sequencer does not call a specific OS or MIDI library.
```

The future renderer follows [UI_DESIGN_POLICY.md](UI_DESIGN_POLICY.md). Step
illumination is driven by timestamped sequencer/audio events and evaluated
continuously from elapsed time, never by render-frame counts. The
`AudioCallbackProcessor` publishes trigger events through the bounded
`StepTriggerEventQueue`, and the UI-side `StepLightBank` consumes them. The
SDL2 application, renderer, and live display integration are not implemented.
Do not render or block inside the audio callback. `ui::StepLightEnvelope` is a
platform-independent elapsed-time envelope helper used by the fixed-size
`ui::StepLightBank`.

`ui::makeMainScreenSnapshot()` prepares a fixed-size, allocation-free copy of
all eight effective track patterns and their UI status for the MAIN renderer.
`ui::layoutMainScreen()` maps this into bounded step-bar geometry and applies
the selected semantic theme for supported viewport sizes.
`ui::buildMainScreenDrawList()` emits validated, fixed-capacity semantic draw
commands for a backend to consume in order. The next-step marker is not a
trigger timestamp; the snapshot, layout, and draw list contain no SDL types,
rendered text, or trigger-light timestamps.

`TickResult` carries each track's step position at the moment that tick was
evaluated. `ui::StepTriggerEventQueue` is a fixed-capacity, single-producer /
single-consumer handoff for copying trigger masks, step positions, master tick
indices, and absolute audio-frame timestamps from the audio side to the UI.
The producer must be the sole audio callback thread and the consumer the UI
thread. The audio backend must check `isLockFree()` before opening the audio
device and reject startup if the target does not provide lock-free atomics. A
full queue drops the newest event and increments a fixed-width overrun counter;
audio processing never waits for the UI. The UI should use the audio frame
timestamp to discard events older than the illumination envelope rather than
replaying stale lights. `AudioCallbackProcessor` publishes events at
`blockStartFrame + sampleOffset`; the UI host must convert audio-frame
timestamps using the active sample rate. Resetting the queue is only safe
while both producer and consumer are stopped; use it with
`StepLightBank::reset()` when establishing a new audio frame timeline.

`ui::StepLightBank` is the UI-side consumer model. It converts absolute audio
frames to envelope timestamps using the active sample rate, retains a future
event until its frame is reached, and drops events older than the complete
illumination envelope instead of showing stale flashes. It stores one envelope
per track and Euclidean step and has no SDL or rendering dependency. The audio
backend must reset both the queue and light bank when starting a new frame
timeline; call their `reset()` methods while audio production is stopped.

## Musical data versus UI and system data

- `TrackState::rhythm` is the stored/base Euclidean configuration.
- `TrackState::lfos` stores up to four independent LFO configurations per track;
  the first-release UI edits slot zero only. Each LFO has exactly one
  destination and one signed amount.
- `TrackState::effectiveRhythm` is the last integer, range-limited modulation
  result. It must not overwrite the base configuration.
- Track phase, master tick position, play state, and LFO runtime phase/seed are
  runtime state. They are not part of persistent project configuration.
- `project::ProjectData` stores musical project state and has its own format
  version.
- `platform::SystemSettings` stores global defaults and platform preferences
  with a different format version. It must not be embedded in project files.
- The selected UI theme is a global display preference, not project data.
  `ui::paletteFor()` maps its stable `ThemeId` to semantic RGBA roles; an
  unsupported ID is reported as unavailable rather than silently selecting a
  different palette. The renderer is responsible for looking up colors by role.
  `SystemSettings` format version 3 includes the selected theme; migration from
  older settings versions will be required when serialization is implemented.
- `ui::UiState` stores screen, focus, modal, and selection state. A UI screen
  change must not change the master-clock phase.

Project and settings serialization are not implemented. `ProjectData` and
`SystemSettings` are configuration models, not a promise that saving or
migration already works.

## Clock, events, and modulation

`platform::ITransportClock` separates clock/transport consumers from the
concrete sequencer. `Sequencer::processFrames` schedules 16th-note ticks from
audio frame counts and sample rate; it does not use the render loop. Each
`TickResult` reports the master tick index and all eight track trigger flags.
An audio engine can consume these results via an `ITriggerSink`-style adapter.

START resets master tick position, all track phases, and LFO runtime phases;
STOP halts clock events. A later START begins all tracks together at step zero.
An LFO-driven pattern-length change normalizes only that track's position with
`position % effectiveLength`. It never resets master transport.

The core separates `LfoWaveformGenerator`, `LfoRateCalculator`,
`ModulationRouter`, and `ModulationEngine` runtime state. Waveforms are Sine,
Triangle, Square, Saw Up, Saw Down, and deterministic Sample-and-Hold.
`FREE` rates are 0.01–1 Hz and independent of tempo. `SYNC` rates follow the
master tempo and support straight, dotted, and triplet whole-, half-, quarter-,
eighth-, and sixteenth-note cycles. Phase advances from elapsed audio-clock
time on each master tick, never from UI frame timing. A tempo change therefore
changes only synchronized phase speed; it does not alter a free-running
frequency.

Sample-and-Hold draws one deterministic per-track/per-LFO xorshift value at
each completed LFO cycle, including when one clock update crosses multiple
cycles. Its initial held value is zero until its first cycle completes.
Multiple LFO contributions assigned to the same destination are summed and
rounded once against the stored base value:

```text
effective = clamp(round(base + sum(lfoValue[i] * amount[i])), validRange)
```

The output is quantized to an integer. `Efill` is additionally limited to the
effective length. When `Eleng` changes during playback, only that track's
position is normalized with `position % effectiveLength`; the master
transport and other tracks retain their phase. `ProjectTrackData` stores its
LFO configurations in project data, separate from global system settings.
LFO runtime phase and PRNG state are not persisted; the optional jitter seed
is a project-stored configuration value. The project data format version is
incremented when musical configuration shapes change.

## Non-destructive jitter

`JitterEngine` processes each track's effective Euclidean pattern after LFO
application and before trigger delivery. It builds a fixed-size event mask and
never edits the stored rhythm. Random decisions refresh at synchronized
intervals of one, two, four, eight, or sixteen master ticks (16th through
whole note); the event mask is composed from the current effective pattern on
each tick, not on UI frames.

INSERT adds events only to steps that were inactive in the source pattern.
DROP probabilistically suppresses active source events. SHIFT moves selected
events by a uniformly selected signed integer number of master ticks in the
configured range, wrapping at that track's pattern boundary. When shifted
events collide, the lowest source-step index wins; duplicate triggers are
discarded. MIX applies DROP, then SHIFT to surviving events, then INSERT only
at originally inactive steps that remain empty. Turning jitter off bypasses
the mask and returns the Euclidean event unchanged.

Intensity is shared by operations in MIX in this initial version. PRNG state
is track-local, optionally seeded from project data, and reset on START/reset
for repeatable playback. STOP freezes it. Tempo changes alter the wall-clock
duration of synchronized intervals but do not reset the random sequence.
Changes to jitter settings invalidate that track's random-decision mask, which
is refreshed at the next tick; the event mask is then composed from the
current pattern. Already delivered trigger events are never changed. Timing
displacement is quantized to master ticks (currently up to eight ticks); no
sub-tick scheduling is claimed by this core.

## Mixer, gain staging, and meters

Each project track stores volume (0–100%) and normalized pan (-1.0 left,
0.0 center, +1.0 right), along with mute. Mono inputs use equal-power panning;
stereo inputs use balance panning to preserve their stereo image. Volume is
mapped quadratically from the UI range to linear gain, then smoothed over 5 ms
to avoid clicks. Track and master gain smoothing starts at silence and ramps
to configured levels, avoiding an initial full-volume transient. Mute applies
a smoothed zero-gain target and never changes sequencer, LFO, or jitter state.

The mixer applies fixed per-track summing headroom of `1 / sqrt(8)` before
summing and a soft limiter at the master output. Master volume is a separate
smoothed gain stage; it is not the mechanism relied on for internal headroom.
Track peaks are measured after track gain/pan, and the master peak is measured
at the limited output. Audio processing publishes meter values through
atomic floats for UI reads; their lock-free status and worst-case callback
cost still require verification on each supported platform. Per-track mixer values are project data;
master volume is a device/global preference in versioned system audio
settings, so changing projects does not change the listening level. Meter
values and runtime smoothing state are not persisted.
The MIXER controller edits track volume/pan/mute and global master volume.
Attach one `audio::AudioParameterQueue` to both `ui::UiController` and
`audio::AudioCallbackProcessor` to transfer supported live changes without
sharing those mutable audio settings with the callback. The fixed-capacity
64-command SPSC queue is produced by the UI thread and drained by the audio
thread at callback block boundaries. It carries track volume, pan, mute,
Attack, Decay, filter enable/cutoff, and master volume. If full, `push()`
rejects the newest edit; `UiState::audioParameterRejected` exposes that
condition and the UI-side value is left unchanged. With the queue attached,
`UiState::audioTrackSettings` is the UI-owned shadow for these controls; it
avoids reading or writing those fields in the audio-owned `Sequencer` while
callbacks are active. A project snapshot coordinator must include that shadow
when assembling project data; serialization and that coordinator are not
implemented yet. The audio backend must check the queue's lock-free atomics
before opening the device. Queue attachment is setup-only, and there must be
exactly one producer and one consumer.
Eight-row mixer drawing and visible peak meters still await the platform
renderer. Other live edits, including rhythm, tempo, instrument assignment,
and modulation, are not yet transferred through this queue and must not be
treated as thread-safe live callback updates.

The Brick Pro has 1 GB RAM shared with NextUI, so the first-playable engineering
targets are bounded: 128 MiB application RSS, 64 MiB decoded sample cache, 32
simultaneous voices, and 30 UI frames/s. These are unmeasured budget candidates,
not current code guarantees. See [PLATFORMS.md](PLATFORMS.md) for callback
deadline and device profiling gates. Audio work must remain bounded and complete
before the obtained SDL buffer deadline; rendering and audio must not allocate
or wait on one another in steady state.

`audio::SamplerVoicePool` provides up to 32 fixed-size mono PCM voices. Samples
are immutable views owned by the loader and must remain alive until all voices
using them have stopped; assigning or replacing them is setup work, never
callback work. It linearly interpolates to the output sample rate and supports
a bounded 0.25–4.0 playback-rate multiplier. When all slots are active, a new
trigger steals the oldest voice, with pool order as the deterministic tie
break. Rendering one track uses caller-owned output storage and performs no
allocation or file access. `audio::WavDecoder` parses bounded RIFF/WAVE chunks
and decodes PCM 8/16/24/32-bit or IEEE float32 mono/stereo, including PCM and
float WAVEFORMATEXTENSIBLE, into caller-provided mono float storage. Stereo is
averaged to mono, finite float samples are clamped to [-1, 1], and malformed,
unsupported, non-finite, or oversized input is rejected before output is
modified. One decoded WAV is capped at 64 MiB; the loader must additionally
be constrained by `audio::SampleCache`, which uses a caller-owned PCM arena and
fixed eight-track metadata to enforce an aggregate 64 MiB cap without
allocation. It performs first-fit allocation into existing gaps and rejects a
load if no single gap can hold the decoded sample; it never compacts or moves
existing samples. Load and unload are setup operations and may only run while
the audio callback and all voices referencing that track are stopped. File I/O
and arena allocation belong to the loader and must run outside the audio
callback.

`audio::SamplerMixerRenderer` connects the sequencer, sampler/synth voices, and
mixer using preallocated fixed-size track/output buffers. `AudioCallbackProcessor`
splits each callback block at scheduled tick sample offsets, renders the
preceding audio segment, triggers the selected instrument for active tracks,
then renders the remainder. Synth tracks require a configured voice; missing
voices or rejected note triggers fail the callback and are counted by the
platform callback. The renderer includes per-track mute/volume/pan, master
volume, peak meters, and the mixer limiter. A shared per-track AD VCA applies
to both sampler and synth output after source rendering and filtering but
before mixing;
the envelope retriggers on each step, finishes at zero, and stops any remaining
source voice. Attack is bounded to 1–250 ms and decay to 20 ms–4 s. Each track
also has an optional one-pole low-pass, bounded to 100 Hz–20 kHz and clamped
below Nyquist for the obtained sample rate; disabled tracks bypass it.
Coefficient work is per block/setting change, while processing is fixed-cost
per sample. Delay effects are not implemented. The renderer, voice pool, mixer,
synth rack, and sequencer are audio-thread owned while the device is open.
Sample load/unload and synth configuration are setup-only and require audio
production to be stopped.

The optional `AudioParameterQueue` is a fixed 64-command SPSC transfer for
track volume, pan, mute, envelope Attack/Decay, filter enable/cutoff, and
master volume. The UI thread produces commands and the callback drains them at
the beginning of each block. A full queue rejects the new command; the UI
controller exposes this through `UiState::audioParameterRejected` and keeps a
separate `UiState::audioTrackSettings` shadow rather than writing those
audio-owned values into the sequencer during playback. Configure the same
queue on the UI controller and callback processor before starting audio, with
exactly one producer and one consumer. The callback preflight rejects a
parameter queue if no renderer is attached or its atomics are not lock-free.
Effects and safe transfer for rhythm, tempo, instrument assignment, LFO,
jitter, and transport changes remain future work.

`ISynthVoice` is the platform-neutral DSP boundary for mono synth backends.
`SynthVoiceRack` holds one externally owned voice per track in a fixed
eight-slot array; any track assigned to synth must have its voice configured
before audio starts, and configured instances must report realtime safety.
Rendering uses bounded mono blocks and must not allocate or block. Synth
output follows the same track mixer and
master limiter path as sampler output. Common synth controls are pitch in Hz
and normalized timbre, color, morph, and level. Attack and decay are provided
by the shared per-track VCA, not duplicated inside each voice backend.

`BasicVaVoice` is the dependency-free reference backend. It has a fixed-cost
monophonic oscillator that morphs between sine approximation, triangle, saw,
and pulse shapes. It rejects pitch above the output Nyquist limit and fills
caller-owned buffers only; the renderer's shared per-track VCA shapes its
output. This is a baseline for integration and profiling; saw/pulse waveforms
are not band-limited, so this prototype may alias and is not a final
sound-quality claim, Braids/Plaits emulation, or claim of target-device
suitability. Adding a third-party engine requires per-file license review and
cross-platform build/resource-generation work, followed by measured
eight-track callback profiling on target hardware.

## Platform ports and future MIDI

- `IAudioBackend` owns platform audio-device lifecycle. DSP, mixing, and
  instrument voices should remain independently testable. The planned SDL2
  implementation uses SDL's audio callback API; ALSA is the initial backend
  candidate for Linux handhelds, selected/configured by the target SDL2 build.
  `open()` receives a non-allocating callback and reports the obtained device
  sample rate and buffer size separately from the request. The callback gets
  interleaved float stereo, the frame count, and the absolute start frame of
  its block; the frame timeline starts at zero on each successful open and
  continues monotonically until close. It must fill the complete output block
  and must not perform blocking work. Validate the obtained spec and lock-free
  requirements before starting playback. Do not make the portable mixer or
  sequencer depend directly on ALSA.
- `audio::AudioCallbackProcessor` is the platform-independent first callback
  adapter. It owns no musical state; the supplied `Sequencer` must be accessed
  exclusively by the audio thread while callbacks run. Each block is
  initialized to stereo silence, scheduled 16th-note ticks are forwarded at
  exact sample offsets to an optional `noexcept`, non-blocking, allocation-free
  callback, and active
  triggers are published to `StepTriggerEventQueue` with absolute audio-frame
  timestamps. Blocks must begin at frame zero and continue contiguously; invalid
  sample rates, oversized blocks, and timeline discontinuities are rejected
  and counted by the platform callback. Check `isRealtimeSafe()` before opening
  the device. Call `resetTimeline()` only after stopping audio production and
  the UI consumer before reopening a device whose frame counter starts at zero;
  reset the UI `StepLightBank` at the same time. When a
  `SamplerMixerRenderer` is supplied, the same adapter also performs
  sample-offset voice triggering and renders sampler/synth output through the
  mixer. Effects and safe UI-to-audio parameter transfer remain future work.
- `IInputBackend` delivers logical UI actions rather than raw device indices.
- `IMidiBackend` transfers generic MIDI events; it must not be implemented
  inside the sequencer using a specific vendor library.
- `ITriggerSink` is the hand-off for sequencer tick results to downstream
  instrument/audio processing.
- `SystemCapabilities` describes which global settings categories a target can
  expose. A UI/platform adapter should hide unsupported options while keeping
  one common settings model.

The interfaces are intentionally small. MIDI Clock In/Out, Start/Stop/
Continue, Note Out, CC mapping, MIDI Learn, and OS-specific adapter behavior
remain future work.

## Real-time and thread-safety work still required

The sequencer avoids pattern allocation while producing ticks. The callback
and renderer are implemented, but integration and thread-safety still require
the following:

- keep file I/O, logging, project/settings serialization, and UI rendering
  off the callback thread;
- keep live UI parameter updates within `AudioParameterQueue` until additional
  parameters have an equivalent bounded handoff;
- avoid blocking locks and unbounded work in the audio callback;
- ensure LFO, rhythm, tempo, instrument, jitter, and transport state are not
  concurrently mutated by the UI thread;
- preserve sample-offset trigger scheduling when changing the callback path;
- verify all of these properties on a host build and on each supported target.

The UI controller directly edits the sequencer for rhythm, tempo, instrument,
LFO, jitter, and transport changes. Those edits are not protected by the audio
parameter queue and must not be made concurrently with
`AudioCallbackProcessor`. The application host must serialize those operations
while audio is stopped, or implement a separate bounded command/snapshot
handoff before enabling them during playback. Queue attachment and UI shadow
initialization are setup operations and must occur before audio starts.
