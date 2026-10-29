# Pocket Euclidean — UI Design Policy

**Version 1.0 · Design Principles**

## 1. Design Concept

### Super Flat. Rhythm as Form. Light as Motion.

Pocket Euclidean is a musical instrument whose interface is built around the
visual structure of Euclidean rhythms.

The interface must not imitate conventional hardware synthesizers, grooveboxes,
or game menus. Treat rhythm as a minimalist visual composition: a distribution
of bars across space, animated by sound and time.

The core visual language consists of:

- **Bars** — rhythm structure and parameter representation.
- **Typography** — labels, values, and navigation.
- **Space** — separation and balance.
- **Light** — playback, emphasis, and temporal movement.

Everything else is secondary. The interface should feel closer to a minimalist
artwork or generative visual instrument than to traditional music software.

## 2. Fundamental Principles

### 2.1 No Decorative Containers

Do not draw a box around every button, parameter, or selectable item. Avoid
button-shaped rectangles, rounded cards, decorative borders, heavy separators,
drop shadows, bevels, gradients, skeuomorphic controls, and unnecessary icons.
Interactive elements should look like text or graphics, not simulated physical
controls.

Communicate interaction through changes in text, position, brightness, or the
state of the relevant graphic—not conventional button chrome.

### 2.2 Flat Bars Instead of Knobs

Represent parameters using simple horizontal bars wherever practical. A
parameter can consist of a short label, a useful numeric value, and a flat bar
indicating its current value. Changing the parameter changes the bar's length or
fill distribution. For example, 25% should occupy approximately one quarter of
the range and 75% approximately three quarters.

Do not introduce circular knobs merely because a parameter is traditionally
controlled by one.

### 2.3 Typography Is Part of the Composition

Use a clean, highly legible sans-serif typeface. Typography should be restrained
and functional; track names, parameter labels, and values remain subordinate to
the rhythm. Avoid excessive font sizes, bold weights, decorative typefaces, and
unnecessary uppercase headings. Establish hierarchy with alignment, spacing, and
consistent positioning rather than boxes and dividers.

One screen uses one type scale. Parameter rows must not mix a large label with a
shrunken value: if a label or value does not fit, shorten or abbreviate the text
and narrow the bar before reducing the glyph scale. Two-column parameter rows
budget eight characters each for the label and the value, and longer file names
carry a trailing `...`; single-column screens may use their extra width. Bar
length is secondary to legibility.

### 2.4 Space Is an Active Element

Empty space is not wasted space. Do not fill every pixel with labels, controls,
or decoration. Leave room around rhythmic patterns so their distribution can be
perceived as a composition. Keep the interface calm and balanced even when
several tracks are active.

## 3. Euclidean Rhythm Visualization

### 3.1 The Rhythm Is the Primary Graphic

The Euclidean sequence is the central visual identity. Represent each track as
a horizontal distribution of equally spaced step bars, understandable without
a conventional grid of step buttons.

- `Eleng` determines the number of step positions.
- `Efill` determines the number of active steps.
- `Eoffs` determines pattern rotation.
- Each step is represented by a simple bar or rectangular mark.
- Active and inactive steps are distinguished through brightness and restrained
  contrast.

The arrangement of bars remains the dominant graphic on MAIN.

The renderer obtains its eight rows from
`ui::makeMainScreenSnapshot()`. Each row contains up to 32 fixed-spaced step
states, effective rhythm parameters, the next-step position, selection, mute,
and instrument state. Unused positions beyond `Eleng` are not drawn. A row's
step spacing must remain consistent with the other rows; do not stretch shorter
patterns to fill the viewport.

`ui::layoutMainScreen()` converts that model and a viewport size into bounded
step-bar rectangles and applies the selected semantic palette. It supports
viewports from 320 x 240 through 16384 x 16384. The `next` marker denotes the
track's next sequencer position, not a timestamped sound event; trigger-light
envelopes still require the separate event handoff described below. Text
rendering, font selection, and SDL integration remain renderer responsibilities.

`ui::buildMainScreenDrawList()` converts the layout to a fixed-capacity,
allocation-free list of semantic commands: clear background, tempo and
transport text roles, track labels, and active/inactive step bars. Commands
retain track selection, mute, instrument, and next-step metadata so a renderer
can express those states consistently. It validates all command bounds before
replacing the caller's draw list. A backend should render commands in order,
format text roles itself, and use the semantic colors provided; the command
list does not perform SDL calls or invent a font.

### 3.2 Active and Inactive Steps

Inactive steps remain visible but quiet. Active steps have greater visual
presence without saturated colors or heavy outlines. Use luminance as the
primary distinction:

- Inactive step: subdued, low contrast.
- Active step: clearly visible at rest.
- Currently sounding step: temporarily illuminated.
- Selected track or editable element: subtly emphasized, without a surrounding
  box.

Each step is one flat fill. Active steps must not add a contrasting cap line,
and inactive steps must not add an inset frame, ruled border, or center dot.
Playback position is conveyed by the fill's luminance, not by an outline.

Keep the pattern legible when playback is stopped.

The step graphic also carries live modulation. When the audio thread reports a
post-LFO effective rhythm, the bars follow it. When jitter drops, inserts, or
shifts triggers, the bars follow the post-jitter event mask and the header adds
a `JIT` marker; `MOD` denotes an LFO-changed rhythm. A trigger that exists only
because jitter inserted or shifted it uses half the cell width and a slightly
accent-tinted fill, so it is never mistaken for a Euclidean step; a dropped
trigger simply reads as a rest.

### 3.3 Rhythmic Distribution as Modern Art

Treat the horizontal sequence as a graphic composition, not just a set of
controls. Keep step spacing consistent within a track. Do not distort spacing
to force tracks of different lengths to occupy the same number of step
positions. Avoid unnecessary grid lines, beat divisions, tick marks, and
decorative backgrounds; add references only when they provide clear musical or
operational benefit.

### 3.4 Playback Illumination

During playback, illuminate the current step in synchrony with the sequencer.
When a step triggers a sound, give its bar a brief, distinct visual response.
The illumination should make the rhythm feel alive without turning the screen
into a conventional blinking step sequencer. The pattern remains stable; only
its light state changes.

## 4. Light and Temporal Behavior

### 4.1 Smooth Light, Discrete Rhythm

Musical events are discrete; their visual response can develop and fade
continuously. Do not use binary on/off blinking as the primary expression of
playback. Use a smooth luminance envelope for triggered steps.

### 4.2 Light Envelope

Use these as initial tuning targets:

| Property | Initial target |
| --- | --- |
| Attack | 10–25 ms |
| Peak hold | 15–40 ms |
| Decay | 120–220 ms |
| Envelope | Smooth and continuous |
| Timing | Independent of render frame rate |

The platform-independent `ui::StepLightEnvelope` currently uses an 18 ms
smoothstep attack, 25 ms peak hold, and 170 ms smoothstep decay. These lie
within the initial target ranges and are implementation defaults, not final
visual tuning. It evaluates luminance from an absolute monotonic timestamp, not
from frame-by-frame increments.

When a step is retriggered before its current envelope ends, the envelope
restarts its attack from the current luminance. This avoids an abrupt brightness
drop while returning the step to peak. A renderer should keep one envelope per
visual step and supply trigger timestamps from sequencer events. Tune the
defaults through device testing; at high tempos, repeated responses should
remain coherent rather than forming an uncontrolled mass of light.

### 4.3 Continuous Luminance

Evaluate the light envelope continuously from elapsed time or timestamps, not
by a fixed increment per rendered frame. A frame-rate drop must not change the
intended response duration. A simple exponential decay or another inexpensive
continuous envelope is suitable. Avoid costly blur, bloom, and post-processing
unless demonstrably necessary. The desired effect is controlled luminance in
the bar itself, with optional very subtle local spill if performance permits.

### 4.4 Separate Visual Motion from UI Transitions

UI transitions are not a design priority. Avoid elaborate slides, animated
panels, zooming menus, spring effects, and animated navigation. Screen changes
may be immediate; selection changes should be direct and unambiguous.

**Invest animation effort in light, not in interface movement.**

## 5. Parameter Interaction

### 5.1 Flat Parameter Controls

Use a consistent grammar, for example:

```text
LABEL   VALUE
━━━━━━━━━━━━──────
```

The example is illustrative; do not draw decorative brackets or heavy outlines.
A minimal flat bar communicates its filled range. Show a numeric value when
useful, especially for BPM, sequence length, fill, offset, and LFO rate.

### 5.2 Editing Feedback

- Update the displayed value immediately.
- Update the bar length or distribution immediately.
- Update the Euclidean pattern immediately when a rhythm parameter changes.
- Avoid pop-up confirmations for ordinary edits.
- Avoid animated number counters unless they prove useful.

A small brightness increase or restrained text emphasis may indicate the
currently edited parameter. Do not surround it with a box.

### 5.3 Parameter-Specific Representations

- **Eleng:** number and spacing of step positions.
- **Efill:** active-step distribution.
- **Eoffs:** pattern rotation.
- **Volume:** horizontal fill bar.
- **Pan:** centered horizontal indicator for left and right balance.
- **LFO amount:** horizontal modulation-depth bar.
- **LFO rate:** bar and numeric value or musical division.
- **Jitter intensity:** horizontal variation bar.

Whenever practical, show a parameter's effect directly in the musical graphic
rather than relying exclusively on an abstract control.

## 6. Color and Contrast

Use a restrained palette: one background, one primary foreground, one subdued
inactive color, and one accent for selected or especially important states.
The renderer must obtain colors by semantic role from
[`ui/ThemePalette.h`](../src/ui/ThemePalette.h), not hard-code RGB values in
individual screens. The built-in choices are Quiet Dark, Warm Paper, High
Contrast, Neon RGB, Outrun, Amber CRT, Phosphor, and VGA DOS; they share the
same roles and can be switched through the global
`SystemSettings::display.theme` preference. The retro-computer presets pair a
near-black background with one saturated RGB phosphor (cyan, magenta, amber,
green, or VGA yellow) and keep the same four semantic roles, so every screen
switches as one piece. Keep the palette set extensible without assigning
different bright colors to each track.

Avoid rainbow track colors, decorative gradients, and a different bright color
for each track. Identify tracks primarily by position and labeling, not color.
Keep the display readable at the TrimUI Brick Pro's native 1024 x 768
resolution, with appropriate scaling or layout adaptation for desktop.

## 7. Screen-Level Application

Apply the same super-flat visual language across all screens.

### MAIN

Display eight tracks as horizontal rows. Each row has its track identifier and
Euclidean step distribution; playback illumination moves through the patterns.
Keep BPM, playback state, and selected-track information small and unobtrusive,
but name the selected track explicitly with a `TRKn` badge rather than relying
only on an unlabeled pip. MAIN uses no bottom hint band: its lanes extend to the
lower margin so the rhythm stays the dominant graphic.
The transport state is carried by one oversized geometric mark beside the BPM
readout: a filled `▶` while the clock runs and two solid bars while it is
stopped. The mark is larger than the BPM text so play/stop reads at a glance
from a distance; the word `PLAY`/`STOP` is never drawn, and the master tick
survives as a small `TICK n` diagnostic line under the BPM.
The groove should be understandable at a glance.

### Transport mark

Every screen repeats the same mark at the right edge of its header, paired with
the current BPM (`120 BPM ▶`). It uses accent while playing and primary while
stopped, is sized from the header height rather than the text scale, and is
built only from axis-aligned rectangles so it stays crisp at 640×480 and
1024×768. Because the glyph is the state, no screen may add a textual
`PLAY`/`STOP` label next to it.

### EDIT

Focus on the selected track's rhythm. Give its pattern enough space that
changes to length, fill, and rotation are immediately visible. Use flat bars or
simple numeric values, not a conventional matrix of boxed step buttons.

### SOUND

Present sound-source and synthesis parameters with the same flat-bar language.
Keep labels concise and values easy to read. The header carries an accent
`TRKn` badge so the edited track is never ambiguous, and the left/right columns
stay visually separate without boxing every row.
When a track uses the SAMPLER instrument, synth-only parameters (ENGINE, MODEL,
PITCH, MORPH, TIMBRE, COLOR) are disabled and rendered in subdued colors without
active fill, rather than shifting the grid rows. When SYNTH is active, SAMPLE is
subdued. Disabled fields cannot be toggled via Confirm.

### MIXER

Use horizontal level bars for track volume and a minimal centered pan indicator.
Up/Down navigates between the eight track strips and the dedicated master volume
strip below, aligning the vertical spatial order with the parameter screens.
Left/Right moves between columns (VOL, PAN, MUTE, LOCK, DICE, REV, DLY); value
changes use the same tracker-style A/Ctrl+Left/Right modifier as SOUND,
MODULATION, and TURING. A tap on A directly toggles MUTE/LOCK or rolls DICE.
Communicate mute and solo states, if implemented, with text or restrained
luminance changes rather than boxed buttons. Peak meters may be flat bars without
ornamental frames. The per-track phrase LOCK and DICE sit immediately right of
MUTE as flat geometric badges in the same strip: LOCK is a padlock whose body
is filled while engaged and outlined while the phrase can still evolve, while
DICE is a die face that is momentary and only brightens when focused. Both use
the same four semantic colors and add no frames or drop shadows.
The master strip spans the full fader width, and its header badge switches from
`TRKn` to `MST`.

### MODULATION

Represent LFO rate, amount, and destinations through compact text and flat bars.
Consider subtly indicating effective values without obscuring base values.

### TURING

Treat the sample-and-hold register as a small generative score: one bar per loop
slot on the left, with the moving head marked by luminance or a single accent
line, and the current semitone offset on the right. The parameter rows keep the
same flat-bar grammar as SOUND and MODULATION. Do not add piano-roll grids,
note-name badges per step, or decorative sequencer chrome; the register's shape
and the head position are the graphic. LOCK is a normal switch row directly
under ENABLE, and the header appends `LOCK` while it is engaged so a frozen
phrase is never mistaken for a dead one. A dice roll is a performance gesture,
so it should read as a change in the bars themselves rather than a dialog; the
SEED row and the MIXER header value show the rolled number as confirmation.

### SYSTEM

Follow the same typography and spacing rules, prioritizing operational clarity.
Avoid decorative UI elements even in file selectors and modal menus.

## 8. Input and Selection

The interface is controller-first and supports keyboard input during desktop
development. Selection must remain understandable without a mouse pointer.
Consistent selection cues may include slightly increased luminance, restrained
text-weight change, a short underline, or a subtle change in the relevant
graphic's brightness. Do not use a different selection style on every screen.
Navigation should be direct and predictable; screen-change animation is
unnecessary.

## 9. Performance and Implementation

The visual system is inexpensive to render and never interferes with audio
timing:

1. Use simple 2D primitives and text rendering.
2. Avoid unnecessary textures, shaders, blur, and full-screen post-processing.
3. Calculate light envelopes from elapsed time, independent of UI frame rate;
   use `ui::StepLightEnvelope` rather than per-frame increments.
4. Drive step illumination from the actual sequencer event timeline, not the
   rendered frame count.
5. Keep audio processing and visual animation independent.
6. Avoid memory allocation and blocking operations in the audio callback.
7. Verify smooth, correctly timed illumination on the actual TrimUI Brick Pro /
   NextUI target.

Favor predictable performance and accurate timing over visual effects.
On the Brick Pro, target a stable 30 frames/s for the first playable build.
Render the bounded MAIN draw list (at most 267 commands) without per-frame
heap allocation; cache any text resources, and let audio retain priority when
the system is under load. Treat 30 frames/s and the command bound as validation
targets, not measured renderer performance.

## 10. Explicit Non-Goals

- A miniature imitation of a hardware groovebox.
- Realistic knobs, switches, or LED hardware.
- Boxed buttons for every action.
- Decorative panels and cards.
- Excessive color coding.
- Complex screen transitions.
- Large glow halos and cinematic bloom.
- Animated graphics unrelated to musical events.
- Dense numerical dashboards.

Reconsider these only if a concrete usability problem cannot be solved within
the existing visual language.

## 11. Design Acceptance Criteria

A UI implementation is consistent with this policy when:

- Euclidean patterns are the first thing the user notices.
- The rhythm is recognizable without reading every parameter label.
- Active and inactive steps are distinguishable without decorative outlines.
- Triggered steps illuminate smoothly rather than simply flashing on and off.
- Light decay feels continuous, controlled, and rhythmically connected.
- Parameter values are understandable and editable without conventional knobs or
  boxed controls.
- All seven screens remain uncluttered.
- The visual language works at the target device's native resolution.
- Animation introduces no audible timing errors or audio-performance
  degradation.

## Final Principle

**The rhythm is the structure. The bars are the image. Light is the movement.
Everything else is quiet.**
