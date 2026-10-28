# UI controls and validation checklist

The UI currently has a platform-independent logical input mapper and a
testable screen-state controller. SDL keyboard/controller events are
translated to the enums in `src/ui/InputMapping.h`; screen behavior dispatches
only logical `ActionEvent` values. The seven primary screens cycle
MAIN → EDIT → SOUND → MIXER → MODULATION → TURING → SYSTEM. The SDL2 adapter and
the seven rendered screens live in `src/app/main.cpp`; this document remains the
authoritative description of logical control behavior.

Visual design requirements for the renderer are in
[UI_DESIGN_POLICY.md](UI_DESIGN_POLICY.md). In particular, pattern-trigger
illumination must be driven by the sequencer event timeline, not UI frame count.

## Default keyboard mapping

| Keyboard | Logical action |
| --- | --- |
| Up / Down | Select a row, track, or category |
| Left / Right | Move between the two parameter columns on SOUND/MODULATION/TURING; adjust the focused value on EDIT/MIXER/SYSTEM |
| Ctrl+Left / Right | Adjust the focused SOUND/MODULATION/TURING value (tracker-style edit modifier) |
| Enter | Confirm / open EDIT from MAIN |
| Escape | Back / close the topmost modal |
| Space | Start/stop |
| M | Mute selected track |
| Q / E | Previous / next track |
| Page Up / Page Down | Previous / next primary screen |
| Tab / Shift+Tab | Next / previous primary screen |
| P | Open project menu |
| F1 | Contextual help |
| Shift+Arrow | Fine value adjustment (combine with Ctrl on SOUND/MODULATION) |

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

Edge-triggered actions (transport, confirm, back, mute, track/screen changes,
project menu, help) fire on the press edge only. Key-up and button-up never
apply them a second time; `InputMapping::shouldDispatch` centralizes that rule
so a toggle cannot be applied twice by one physical press. The Shift modifier
is the exception: both edges update fine-adjust state. When the window loses
focus or is minimized, the adapter clears held repeats, the Shift and Ctrl
modifiers, the Select+Start quit combination, and analog trigger latches,
because the matching release event may never arrive. Device disconnect does
the same.

## Default logical controller mapping

| SDL GameController logical control | Logical action |
| --- | --- |
| D-pad | Navigate |
| A | Confirm; hold with Left/Right to adjust a SOUND/MODULATION/TURING value |
| B | Back |
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
  EDIT; Play/stop, mute, and primary-screen navigation are available.
  The selected track is named in the header (`TRK1` … `TRK8`) next to the
  Euclidean summary. MAIN does not draw a bottom footer: its eight lanes use
  the full viewport height so the rhythm remains the dominant graphic.
  START and Space are a toggle, not press-and-hold: the first press resets
  every track and the master clock to step zero and starts playback; the next
  press stops the clock. Releasing the key or button does nothing, and OS
  key-repeat cannot toggle again.
  While playing, the step bars show the post-LFO, post-jitter triggers the
  audio thread actually fires. `MOD` in the header summary means the LFO
  changed the effective rhythm; `JIT` means jitter moved, dropped, or inserted
  at least one trigger. Active steps are one solid flat fill (no accent cap),
  and rest steps have no inset border or center dot; playback position is shown
  by the current step's fill brightness, not by an outline. A trigger that only
  exists because jitter inserted or shifted it is drawn at half the cell width
  with an accent-tinted fill; dropped triggers read as rests.
- EDIT: Up/Down moves through length, fill, offset, instrument, volume, and
  mute. Left/Right edits the focused field; length is 2–32, fill is
  1–length, offset is 0–31, and volume is 0–100. Reducing length clamps fill.
  The large step explorer and the lower polymetric matrix use the same live
  post-jitter triggers as MAIN, and the banner carries the `JIT` indicator.
- SOUND: Up/Down selects per-track Attack, Decay, Filter Enable, Filter
  Cutoff, Synth Engine, Plaits Model, Synth Pitch, Synth Morph, Synth Timbre,
  Synth Color, Sample, Delay Time, Delay Feedback, and Delay Mix fields.
  Left/Right moves between the left synthesis column and the right
  timbre/effect column at the same row; the header badge (`TRK1` … `TRK8`)
  names the track being edited. Hold Ctrl (keyboard) or A (gamepad) while
  pressing Left/Right to change the selected value; Shift makes that change
  fine on the keyboard. A quick Confirm/A tap still opens the browser or
  toggles a discrete field. Attack spans 1–250 ms and Decay spans
  20 ms–4 s. Coarse changes are 10 ms for Attack, 100 ms for Decay, 10 Hz for
  synth pitch, and 1000 Hz for cutoff; fine changes are 1 ms, 10 ms, 1 Hz, and
  100 Hz. Cutoff spans 100 Hz–20 kHz and is limited below Nyquist during
  rendering.
  Filter Enable and Synth Engine are discrete: Ctrl/A+Left/Right or Confirm
  toggle them between OFF/ON and BASIC-VA/PLAITS. Plaits Model is an independent
  index field (1-based number plus a four-letter name, e.g. `1 VA`, `8 HHAT`)
  that wraps through all sixteen engines; the model applies only while the
  engine is PLAITS, and Morph remains a continuous parameter *inside* the
  selected model. Both engines run through the same per-track AD VCA and
  filter.
  The SAMPLE row shows the track's WAV file name or `BUILT-IN`. Long names are
  shortened to eight characters with a trailing `...` so the row keeps the same
  font scale as every other parameter. Confirm opens the SELECT WAV browser;
  Up/Down moves the cursor, Confirm opens a directory
  (with a `..` entry for the parent) or assigns the highlighted `.wav` to the
  selected track, and Back cancels. The sample is decoded on the UI thread and
  handed to the audio thread through the sampler's bounded swap queue, so it
  can be replaced during playback; the previous buffer is released only after
  the audio thread acknowledges the swap.
  When an audio parameter queue is attached, supported edits are handed
  to the audio callback at the next block boundary. If the bounded queue is
  full, the edit is rejected and `UiState::audioParameterRejected` is set for
  the frontend to report. `UiState::audioTrackSettings` retains the UI-side
  value while the callback owns the runtime setting; project snapshot/save
  code must include it. Back returns to EDIT.
- MIXER: Up/Down selects one of eight tracks. Confirm cycles Volume, Pan,
  Mute, Reverb Send, Delay Send, and Master Volume; Left/Right adjusts the
  focused value. The header carries the `TRKn` badge and the focused field
  name/value without repeating the track number. The MUTE button in each strip
  shows the current mute state.
  The global MuteToggle action remains a one-action shortcut. Track volume, pan,
  mute, and both effect sends are project state; master volume is a global
  system audio preference. Reverb and delay sends are post-fader (after track
  volume and pan) and are processed by dedicated effect buses before the
  master output.
  Controller and keyboard inputs use the same logical actions. With an audio
  parameter queue attached, changes are applied by the audio thread at the
  next block boundary; queue-full edits are rejected and exposed through
  `UiState::audioParameterRejected`. The UI-side mixer values are in
  `UiState::audioTrackSettings`. Mute gates the mixed output without stopping
  sequencer triggers or modulation.
- MODULATION: Up/Down selects the first LFO's fields followed by the selected
  track's jitter fields. Left/Right moves between the LFO column and the
  jitter column; hold Ctrl (keyboard) or A (gamepad) while pressing Left/Right
  to change a value. The header badge names the track being edited. LFO fields
  include enable, waveform, sync (`LFO SYNC`), rate/division, destination, and
  amount. The destination is exactly one of Eleng, Efill, or Eoffs. Jitter
  enable and intensity follow immediately after the LFO fields; mode,
  synchronized rate, shift range (`SHIFT`), and optional deterministic seed
  controls are also available. Enter toggles the focused enable option. The
  seed value is shown as eight hex digits so all 32 bits stay visible on one
  line. The data model supports four LFOs per track, while the initial UI
  exposes only the first.
- TURING: Up/Down selects the selected track's sample-and-hold phrase fields:
  ENABLE, CLOCK, RATE, LENGTH, CHANCE, RANGE, SCALE, ROOT, GLIDE, DRIFT,
  CST SEED, and SEED. Left/Right moves between the two columns; hold Ctrl
  (keyboard) or A (gamepad) while pressing Left/Right to change a value.
  CLOCK chooses TEMPO (16th-note transport ticks), EUCLID (the track's
  post-jitter Euclidean triggers), or FREE (an internal Hz clock that ignores
  tempo). RATE is interpreted per clock mode: musical divisions for TEMPO,
  trigger multiples for EUCLID, and 0.5–8 Hz for FREE. LENGTH is 2–32 steps,
  CHANCE is 0–100% and controls how often the byte under the head is
  re-randomised (0% locks an exact loop, 100% changes every step), RANGE is
  ±1–24 scale degrees, and SCALE/ROOT quantise the register to a musical key.
  GLIDE is 0–100% portamento (up to 0.5 s) applied to held sampler and synth
  voices; DRIFT slowly transposes the whole phrase by up to one octave across
  loop passes. SEED is eight hex digits and CST SEED selects the deterministic
  seed. A quick Confirm/A tap reseeds the register as a performance gesture;
  on ENABLE and CST SEED it toggles the switch instead. The left card draws the
  live 32-byte register with the moving head, and the right card shows the
  current semitone offset. The `TRKn` badge names the edited track.
- SYSTEM: Up/Down navigates Clock/Transport, MIDI, Audio, Input/Controls,
  Display, Project/Storage, and About/Diagnostics categories. Settings are
  persisted in the versioned v4 system file, separately from project data. In
  the Clock/Transport category, Left/Right changes the initial tempo. In the
  MIDI category, Left/Right disables/enables MIDI clock and transport input;
  the row shows `OFF`, `ON`, or `NO DEV` when no port could be opened. This
  preference is independent of project dirty state. The controller can skip
  categories unsupported by its supplied platform capabilities (Linux /
  PortMaster hides MIDI).
- Display category: Left/Right cycles the global theme (Quiet Dark → Warm
  Paper → High Contrast → Neon RGB → Outrun → Amber CRT → Phosphor → VGA DOS →
  Quiet Dark); this preference is also independent of project dirty state.
- Help, project-menu, WAV file-selector, and discard-confirmation modal states
  exist. The project menu emits explicit requests for the project manager;
  save/load serialization and scan-free rendering are implemented in the SDL
  app. Directory enumeration is requested by the controller and performed by
  the app layer. Unsaved-change confirmation defaults to Cancel, and the UI
  does not clear its modified flag until the project layer reports success.

## Automated GUI checks

`pocket_euclidean_app.exe --capture <file.bmp> --capture-delay <ms>` renders one
frame and exits. `--inject-keys <spec>` pushes synthetic SDL key events into the
normal event queue before that frame, so the real adapter code (including
press/release edge handling) runs without a human at the keyboard. The spec is a
comma-separated list of `key[:action]`, where key is `up`, `down`, `left`,
`right`, `enter`, `escape`, `space`, `m`, `q`, `e`, `tab`, `pageup`, `pagedown`,
`p`, `f1`, `shift`, or `ctrl`, and action is `tap` (default), `down`, `up`, or
`repeat`. Events fire 300 ms apart starting at 400 ms. Examples:

- `--inject-keys space:tap` → header shows `PLAY` (one press starts).
- `--inject-keys space:down,space:up` → header still shows `PLAY` (release does
  nothing; this is the press-and-hold regression).
- `--inject-keys space:tap,space:tap` → header shows `STOP` with a non-zero
  master tick, proving the first press ran and the second stopped.
- `--inject-keys space:down,space:repeat` → header still shows `PLAY` (OS
  key-repeat cannot toggle transport twice).
- `--inject-keys ctrl:down,right:tap,ctrl:up` → on SOUND the selection stays on
  ATTACK while its value steps up (tracker-style edit), instead of moving to the
  right column.

## Manual test checklist

- [ ] Verify Brick Pro logical GameController mappings on the exact NextUI /
  PortMaster versions selected for release; verify other supported device
  profiles independently.
- [ ] Navigate all eight tracks using D-pad and keyboard; confirm wraparound.
- [ ] Edit length, fill, offset, instrument, and volume with keyboard and pad.
- [ ] Adjust per-track SOUND Attack/Decay values with coarse and fine input;
  verify settings persist independently across all eight tracks.
- [ ] On SOUND, MODULATION, and TURING, move between the two columns with
  Left/Right and verify the value does not change; hold A (pad) or Ctrl
  (keyboard) with Left/Right and verify the value changes without leaving the
  focused row.
- [ ] Verify the `TRKn` header badge matches the selected track on MAIN, EDIT,
  SOUND, MIXER, MODULATION, and TURING.
- [ ] Enable the per-track low-pass in SOUND, adjust cutoff, and verify
  bypass/filtered behavior and independent settings for all tracks.
- [ ] Reduce length below fill and verify fill clamps immediately.
- [ ] Switch MAIN → EDIT → SOUND → MIXER → MODULATION → TURING → SYSTEM → MAIN
  and confirm track selection persists.
- [ ] Switch across all seven primary screens and inspect all seven system
  settings categories.
- [ ] Modulate each rhythm destination and verify base values remain unchanged,
  effective values clamp/quantize, and master transport phase is preserved.
- [ ] Verify jitter Enable and Intensity are directly reachable in MODULATION,
  and that Off reproduces the stored Euclidean pattern exactly.
- [ ] On TURING, enable the generator and verify TEMPO, EUCLID, and FREE clocks
  advance the register at their own rates. Confirm that CHANCE 0% loops the
  phrase exactly, 100% rewrites every step, and a Confirm tap reseeds the
  register without dirtying the project. Verify GLIDE is audible on held notes
  and that DRIFT only moves the phrase at loop boundaries.
- [ ] With jitter enabled, verify MAIN and EDIT step bars follow the live
  post-jitter triggers and the `JIT` indicator appears when the pattern differs.
- [ ] Verify jitter-inserted or shifted-in triggers render as half-width,
  accent-tinted steps, while Euclid steps keep the full cell width.
- [ ] Verify active steps are one solid fill and rest steps have no accent cap,
  inset border, or center dot.
- [ ] Navigate all eight mixer tracks; adjust volume/pan and reverb/delay
  sends, toggle mute without changing transport phase, and verify master
  volume persists across projects.
- [ ] Verify mono equal-power and stereo balance panning, smooth gain changes,
  peak meter updates, and limiter behavior on the audio callback.
- [ ] Start/stop using Start and Space; verify the first press starts from step 0,
  the next press stops, releasing the key does nothing, and repeated key-down
  does not toggle again.
- [ ] On SOUND, toggle Synth Engine with Ctrl/A+Left/Right and Confirm; verify BASIC-VA
  and PLAITS both sound and that the MODEL field selects each of the sixteen
  Plaits engines independently of Morph.
- [ ] On SOUND, open SAMPLE, browse directories, assign a WAV to a track, and
  verify the row shows the shortened file name. Save the project, reload it, and
  confirm the same sample is restored; verify a missing path falls back to
  BUILT-IN and that a long name keeps the row's font scale.
- [ ] On every parameter screen, verify all rows share one glyph scale and that
  the widest label/value (`WAVEFORM`, `STRENGTH`, `TRIANGLE`, `PAD FIRST`) does
  not shrink on its own at 1024×768 and 640×480.
- [ ] Replace a track's sample while playback is running and verify no dropout,
  callback failure, or use-after-free of the previous buffer.
- [ ] Lose window focus while holding a D-pad direction; verify repeat stops and
  no transport state is left latched.
- [ ] Mute/unmute using X and M.
- [ ] Hold a direction and verify configurable delay and acceleration.
- [ ] Open/close Help and Project Menu; verify modal input does not reach the
  underlying screen.
- [ ] Connect project requests to persistence; verify save/load/new completion
  and failure paths update the dirty flag only after successful operations.
- [ ] Test the primary Brick Pro layout at 1024×768 on the exact NextUI /
  PortMaster versions selected for release, and test scaling on at least one
  desktop OS.
- [ ] Switch the global display theme through all built-in presets (Quiet Dark,
  Warm Paper, High Contrast, Neon RGB, Outrun, Amber CRT, Phosphor, VGA DOS);
  verify every screen uses the selected semantic palette and the
  choice does not change when opening another project.
- [ ] Apply [UI_DESIGN_POLICY.md](UI_DESIGN_POLICY.md): confirm patterns are
  visually primary, selection is clear without boxed controls, and active and
  inactive steps remain legible.
- [ ] Trigger steps at multiple tempos and render frame rates; verify smooth
  light with the documented attack/hold/decay targets, without binary-blink
  playback or audio timing impact.
- [ ] Validate project file operations and destructive-action confirmation
  after project management is implemented.
