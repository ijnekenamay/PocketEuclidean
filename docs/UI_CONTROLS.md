# UI controls and validation checklist

The UI currently has a platform-independent logical input mapper and a
testable screen-state controller. SDL keyboard/controller events should be
translated to the enums in `src/ui/InputMapping.h`; screen behavior should
dispatch only logical `ActionEvent` values. The six primary screens cycle
MAIN → EDIT → SOUND → MIXER → MODULATION → SYSTEM. The SDL adapter and rendered
screens are not implemented yet.

Visual design requirements for the renderer are in
[UI_DESIGN_POLICY.md](UI_DESIGN_POLICY.md). In particular, pattern-trigger
illumination must be driven by the sequencer event timeline, not UI frame count.

## Default keyboard mapping

| Keyboard | Logical action |
| --- | --- |
| Up / Down / Left / Right | Navigate, or adjust the focused EDIT value |
| Enter | Confirm / open EDIT from MAIN |
| Escape | Back / close the topmost modal |
| Space | Start/stop |
| M | Mute selected track |
| Q / E | Previous / next track |
| Page Up / Page Down | Previous / next primary screen |
| Tab / Shift+Tab | Next / previous primary screen |
| P | Open project menu |
| F1 | Contextual help |
| Shift+Arrow | Fine value adjustment |

OS key-repeat events are ignored for non-directional actions, so holding Space
does not repeatedly toggle transport. Use `InputRepeater` for directional
repeat: it emits immediately, waits the configured initial delay, repeats at
the normal interval, then accelerates after the configured hold duration.
The SDL adapter must pass key-down/key-up transitions and poll it from the
application loop. `KeyboardKey::Shift` key-up clears fine-adjust mode.
`InputRepeater` tracks up to 32 simultaneous repeatable controls independently.
Repeated key-down events for an already-held control do not restart its delay.
When full, a new directional press is still emitted once but is not held for
repeat; release events only cancel the matching source/control pair.

## Default logical controller mapping

| SDL GameController logical control | Logical action |
| --- | --- |
| D-pad | Navigate |
| A / B | Confirm / Back |
| X / Y | Mute / Help |
| L1 / R1 | Previous / next track |
| L2 / R2 | Previous / next screen |
| Start / Select | Play/stop / project menu |

Map using SDL GameController logical controls when available; do not assume
raw joystick button indices or physical A/B/X/Y positions. A joystick adapter
for controllers without a recognized GameController mapping is still needed.
`ControllerMappingProfile` provides a fixed-size logical-control-to-action
profile, supports rebinding and disabling controls, and has a default mapping
matching the table above. Unknown control or action values are rejected rather
than falling through to a potentially destructive action. Device-specific
profile selection and persistence remain platform/settings-layer work; do not
persist raw SDL or joystick button numbers in the shared profile.

## Current UI state behavior

- MAIN: Up/Down selects one of eight tracks with wraparound; Confirm opens
  EDIT; Play/stop, mute, and primary-screen navigation are available. START
  resets every track and the master clock to step zero; STOP halts the clock;
  pressing START again resets and starts from step zero.
  Pressing START while already playing leaves the current phase unchanged.
- EDIT: Up/Down moves through length, fill, offset, instrument, volume, and
  mute. Left/Right edits the focused field; length is 2–32, fill is
  1–length, offset is 0–31, and volume is 0–100. Reducing length clamps fill.
- SOUND: Up/Down selects per-track Attack, Decay, Filter Enable, and Filter
  Cutoff fields; Left/Right changes the selected value. Attack spans 1–250 ms
  and Decay spans 20 ms–4 s. Coarse changes are 10 ms for Attack, 100 ms for
  Decay, and 1000 Hz for cutoff; fine changes are 1 ms, 10 ms, and 100 Hz.
  Cutoff spans 100 Hz–20 kHz and is limited below Nyquist during rendering.
  When an audio parameter queue is attached, supported edits are handed to the
  audio callback at the next block boundary. If the bounded queue is full, the
  edit is rejected and `UiState::audioParameterRejected` is set for the
  frontend to report. `UiState::audioTrackSettings` retains the UI-side value
  while the callback owns the runtime setting; project snapshot/save code must
  include it. Back returns to EDIT. Instrument waveform, pitch, and effect
  editing are not implemented yet.
- MIXER: Up/Down selects one of eight tracks. Confirm cycles Volume, Pan,
  Mute, and Master Volume; Left/Right adjusts the focused value. The global
  MuteToggle action remains a one-action shortcut. Track volume, pan, and mute
  are project state; master volume is a global system audio preference.
  Controller and keyboard inputs use the same logical actions. With an audio
  parameter queue attached, changes are applied by the audio thread at the
  next block boundary; queue-full edits are rejected and exposed through
  `UiState::audioParameterRejected`. The UI-side mixer values are in
  `UiState::audioTrackSettings`. Mute gates the mixed output without stopping
  sequencer triggers or modulation.
- MODULATION: Up/Down selects the first LFO's fields followed by the selected
  track's jitter fields; Left/Right changes values. LFO fields include enable,
  waveform, rate mode, rate/division, destination, and amount. The destination
  is exactly one of Eleng, Efill, or Eoffs. Jitter enable and intensity follow
  immediately after the LFO fields; mode, synchronized rate, shift range, and
  optional deterministic seed controls are also available. Enter toggles the
  focused enable option. The data model supports four LFOs per track, while
  the initial UI exposes only the first.
- SYSTEM: Up/Down navigates Clock/Transport, MIDI, Audio, Input/Controls,
  Display, Project/Storage, and About/Diagnostics categories. The versioned
  settings model exists separately from project data; rendering and persistence
  are not implemented. In the Display category, Left/Right cycles the global
  theme (Quiet Dark → Warm Paper → High Contrast → Quiet Dark); this preference
  is independent of project dirty state. The controller can skip categories
  unsupported by its supplied platform capabilities.
- Help, project-menu, file-selector, and discard-confirmation modal states
  exist. The project menu emits explicit requests for a future project
  manager; file browsing, serialization, and confirmation rendering are not
  implemented. Unsaved-change confirmation defaults to Cancel, and the UI
  does not clear its modified flag until the project layer reports success.

## Manual test checklist for the future SDL adapter

- [ ] Verify Brick Pro logical GameController mappings on the exact NextUI /
  PortMaster versions selected for release; verify other supported device
  profiles independently.
- [ ] Navigate all eight tracks using D-pad and keyboard; confirm wraparound.
- [ ] Edit length, fill, offset, instrument, and volume with keyboard and pad.
- [ ] Adjust per-track SOUND Attack/Decay values with coarse and fine input;
  verify settings persist independently across all eight tracks.
- [ ] Enable the per-track low-pass in SOUND, adjust cutoff, and verify
  bypass/filtered behavior and independent settings for all tracks.
- [ ] Reduce length below fill and verify fill clamps immediately.
- [ ] Switch MAIN → EDIT → SOUND → MIXER → MODULATION → SYSTEM → MAIN and confirm
  track selection persists.
- [ ] Switch across all six primary screens and inspect all seven system
  settings categories.
- [ ] Modulate each rhythm destination and verify base values remain unchanged,
  effective values clamp/quantize, and master transport phase is preserved.
- [ ] Verify jitter Enable and Intensity are directly reachable in MODULATION,
  and that Off reproduces the stored Euclidean pattern exactly.
- [ ] Navigate all eight mixer tracks; adjust volume/pan, toggle mute without
  changing transport phase, and verify master volume persists across projects.
- [ ] Verify mono equal-power and stereo balance panning, smooth gain changes,
  peak meter updates, and limiter behavior on the audio callback.
- [ ] Start/stop using Start and Space; verify every start resets all tracks
  together to step zero and repeated key-down does not toggle twice.
- [ ] Mute/unmute using X and M.
- [ ] Hold a direction and verify configurable delay and acceleration.
- [ ] Open/close Help and Project Menu; verify modal input does not reach the
  underlying screen.
- [ ] Connect project requests to persistence; verify save/load/new completion
  and failure paths update the dirty flag only after successful operations.
- [ ] Test the primary Brick Pro layout at 1024×768 on the exact NextUI /
  PortMaster versions selected for release, and test scaling on at least one
  desktop OS.
- [ ] Switch the global display theme among Quiet Dark, Warm Paper, and High
  Contrast; verify every screen uses the selected semantic palette and the
  choice does not change when opening another project.
- [ ] Apply [UI_DESIGN_POLICY.md](UI_DESIGN_POLICY.md): confirm patterns are
  visually primary, selection is clear without boxed controls, and active and
  inactive steps remain legible.
- [ ] Trigger steps at multiple tempos and render frame rates; verify smooth
  light with the documented attack/hold/decay targets, without binary-blink
  playback or audio timing impact.
- [ ] Validate project file operations and destructive-action confirmation
  after project management is implemented.
