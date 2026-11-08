# GitHub snapshot — 2026-11-08

This private repository contains the current authored source and portable tests.
It does not contain original binaries, licenses, factory content, local captures,
or built app/VST bundles. It is not a release.

The latest recorded staged candidate is batch 58. Its ten runnable suites,
pluginval strictness 10, fifteen actual-VST MIDI cases and standalone checks
passed; private factory playback was skipped. Batch 60 added a passing focused
pattern-trigger integration check. Batch 62 corrected native Init object
ownership in the investigation; exact native reset samples and hidden defaults
remain unresolved. These are historical candidate results, not a certification
of every change in the current working source.

A fresh source-only export was configured and built on 2026-11-08 with the
plug-in disabled and two compile jobs. All five core CTest suites passed.
The full VST3/standalone build was not repeated for this GitHub sync.

Full sound and visual parity remain open. Local audit reports and captures stay
outside GitHub; the notes below retain their measurements for context.

---

# Current status

Historical audit notes updated through 2026-11-03. This project is staged privately for continued development.
There is no public release or claim of complete compatibility.

The historical standalone/VST pair described below is **batch 51**. It preserves the native
float multiplication order before the shared Aux3 distortion arctangent,
reducing maximum-drive waveform error from 2.93% to 1.34% in the saved native
comparison. Batch 52 then isolated PCM24 input quantization: using a new
floating-point native capture, the unchanged processor measures 0.000862%
maximum-drive error (48 kHz, 128-frame callbacks). Exact sound identity
across the full product remains open. The
auxiliary report (local audit record) records the repair
and measurements. Previous staged candidates remain available.

Batch 53 captured the original Intruders Must Perish / Fat pattern locally:
peaks 1.823298/1.821530, RMS 0.359205/0.359501. The current batch 51 processor
fixture has peak 1.695521 and RMS 0.269142 for the same preset and duration.
Above-unity output alone is therefore not a limiter defect. The roughly 25.1%
RMS difference and native repeat variation remain unresolved. The measurement
host now supports float WAV output; a source-only Modal build and synthetic
actual-VST test confirm above-unity preservation and the default PCM24 path.
No production DSP or GUI change was made in this batch.

The 18-video ledger (local audit record),
sidebar report (local audit record),
sound controls report (local audit record) and
manual section report (local audit record) track the
reference checks. Older batch reports below are historical evidence.

## Official tutorial comparison

Batch 19 corrected pie orientation, legacy Room/Reverb sends, standalone
Spacebar, the imported kit header, lane-control size and sampler styling using
Modalics' Basic Beat Making & Editing, Presets & Kits, and Samples & Sounds.
The same tutorial preset now shows Clear SvN and Kick Room 41% / Reverb 17%.
Fonts, icons, colors and spacing still differ in places; complete visual identity
is not established.

The repaired actual-VST render at 100 BPM has rhythm-envelope correlation
0.906 and waveform correlation 0.621 with the selected tutorial interval.
These are similarity measures, not correctness scores. Low-frequency energy
still differs, and exact sound identity remains unproven because the recording
chain and version/preset equivalence are uncontrolled.

## What has been checked

The [batch 51 source-only Modal run](https://modal.com/apps/snorkelai/main/ap-Lnsw535Aiiw0eqCKy8fwW2)
passed configure/build, ten available CTest suites (private factory suite skipped
remotely), pluginval strictness 10, fifteen actual VST3 MIDI cases, state/audio
smoke and isolated standalone settings, gestures, flam and project recall.
All 55 frozen/current files, remote source hashes and staged binary hashes match.

Local shared-distortion comparisons against saved native renders at 48 kHz and
128-frame callbacks have waveform errors 0.01293% (Drive 0),
1.33733% (Drive 1), and 0.01055% (half-send), all zero lag.
Batch 52 supersedes the maximum-drive PCM24 residual with a floating-point
comparison: 0.00086167% error, passing a 0.01% gate. All nine native dry/wet/
dry-return captures repeat exactly over four seconds. No production change.
These processor comparisons do not prove host sound or complete tutorial identity.

Whole-interface visual/sound identity, real DAW MIDI interchange, native desktop
picker input and physical devices remain unverified. Batch 50 completed all 244 patterns from 129 private presets with no reported
playback failures. Twelve patterns reached unity (peak 1.694586); original sound
parity and clipping-free output are not established. The following batch 19 results
are historical.

In batch 19, all 244 patterns from 129 private factory presets passed locally at 48 kHz
with Room/Plate responses loaded, rendering at least a cycle and four seconds
per pattern. 71 patterns reached or exceeded unity; the largest absolute peak was
3.618424. This checks loading, finite output and expected notes, not original
sound parity or absence of clipping. The separate tutorial render had no
non-finite samples or samples above unity.

The local tutorial capture probe passed 14 checks, including the reference
preset's kit identity, effect sends and tempo. The same-preset screenshot,
large lane controls and actual standalone playback capture were inspected.
Only reconstruction source, synthetic tests and build tools went to Modal;
private samples, responses, original binaries and licenses stayed local.

Earlier batch-16 private Room/Plate checks passed at 44.1/48/96 kHz, including
block-size consistency. Batch 18 verified real mouse meter entry and restart
recall. These checks were not repeated here. Their historical evidence is in the
November 1 audit (local audit record).
Physical audio/MIDI and the active REAPER session remain untested and untouched.
The new candidate is staged separately from installed copies.

## Repairs already in the source

- Temporary slash, Alt and number shortcuts preserve the selected Slicer
  division. Actual beat/lane input and one-step undo pass in an isolated window.
- The sample-folder button opens Load Custom Sample directly, matching the
  tutorial. It shares the pad-menu chooser and retains host-state/lifetime guards.
- Randomizer Spacing now separates hits at high settings and continues phrases
  at low settings, including Fill Empty across beats. Its exact curve is inferred.
- Randomizer Min/Max and toolbar velocity display and accept MIDI 0–127 values;
  Trigger Chance uses percent. Saved normalized precision and exact Alt-reset
  are preserved; the three Randomizer value dials use rising circular fill.
- Bar and pattern context menus include the tutorial commands, shared clipboard
  actions, insertion and one-step undo. Insertion placement/defaults are inferred.
- Held C copies the whole beat in every brush mode. Held V previews it and
  drag-fills visited beats in one undo step; empty/identical pastes are no-ops.
- Linux Ctrl+Z/Ctrl+Y use key codes, so real X11 control-character text no
  longer prevents undo/redo. Actual standalone input and restart pass.
- Pie drawing and picking use the tutorial's six-o'clock clockwise origin;
  imported legacy Aux1/Aux2 map to Room/Reverb without swapping native states.
- Standalone unmodified Space toggles Play/Pause with text-entry and host guards.
- Imported presets resolve a unique factory kit identity, preserve embedded
  sounds and honor kit lock; the tutorial preset now reads Clear SvN.
- Lane controls use the measured smaller dimensions, and sampler body/selector
  styling is closer to the official tutorial captures.

- Double-click accepts typed numerator and denominator values in both the main
  bar header and arrangement pads. Invalid values stay editable; cancellation,
  unchanged values and stale targets leave the project and undo history intact.

- Drive knob travel follows the native gain-domain range centered at 12 dB;
  intermediate markers and inverse mouse conversion now agree.

- Room and Plate now use shared stereo convolution with verified local responses,
  native resampling/normalization and +3/+6 dB main-bus returns. Both routing
  modes receive the shared returns before master processing.

- Master Filter uses the native two-stage low-pass response and cutoff/Q ranges.
  Raw preset import and exact resonance Alt-click reset/undo are repaired.
- Native pad and master compressor envelopes, curves and smoothing replace the
  earlier approximations. Pad compression follows voice summing and pan.
- Pad volume follows the native 100 ms ramp and fixed 0.3 output trim while
  preserving stored/UI gain. The unsupported master hard clip is removed.
- Drive uses the native insert curve, 0–24 dB range and 100 ms post-gain ramp,
  independently of the parallel Aux3 send. Master Crush follows its native
  nonlinear rate/depth, raw hold and nearest-even quantization behavior.
- Turning a note off and back on remembers its velocity. Fresh notes use the
  current default, as does assigning a different pad.
  Saved states retain this distinction; older states keep their stored levels.
- Shift-click fills a partly empty beat or empties a full one in all four brush
  modes, without subdividing or randomizing it. The Fill / empty menu also
  changes the whole beat uniformly and preserves its pad assignments.
- Invalid bar-meter edits and native imports fail before changing the project
  or its undo history. Fractional, string, and overflowing inputs are rejected.
- The Trigger Chance label scales with the compact editor and stays readable.
- Standalone Play/Pause/Stop and tempo follow the app's own clock. The audio
  device's continuously advancing clock no longer overrides those controls.
- Standalone projects saved with multiple outputs play through the main stereo
  output. Restored and subsequently saved routing agrees with that behavior;
  the VST3 keeps multiple outputs and host timing.
- Corrected factory sample resource assignments and the chord-versus-sector
  drawing error. Batch 19 also corrects the tutorial-derived pie orientation.
- Fixed event loss in dense patterns and block-size-dependent playback.
- Repaired host-start and seek alignment, pattern triggers, and MIDI boundaries.
- Corrected several undo, automation, routing, sample-history, and state paths.
- Added host drum-note names and the documented dark and MIDI-display options.
- Subdivision and velocity drags publish each change to playback, with one Undo
  step per changed drag and none for an unchanged click.
- MIDI Note Off releases its matching held voice without cutting overlapping
  notes, sequenced hits, or pad previews. Stolen and finished voices keep their
  release pairing.
- Muted pads and pads excluded by Solo stop feeding the shared Room/Plate sends
  and do not light the pad meter. Existing shared ambience tails continue;
  sampler-disable and reset clear them.
- Sampler dividers follow the manual's control groups without crossing knobs.
- Keyboard rows now match the documented lower and upper pad rows.
- Arrangement items remain reachable by scrolling; hit targets and duplicate
  controls follow the visible items. Right-clicking a delete corner opens its
  menu without deleting an item.
- Zoom buttons and scroll rails control the visible beats and lanes. Scrolled
  lane controls stay within the pattern area, and small editor sizes retain the
  complete title and One Shot label.
- Invalid bar signatures and malformed randomizer indices are rejected before
  changing project state. Compressed state expansion and file reads are bounded.
- Undecodable embedded samples are rejected before replacing state or sound.
  Factory migration skips symbolic links and non-regular files.

Earlier core, state, and editor regressions reproduced the meter, velocity,
and fill/empty defects on the earlier source and passed after repair on Modal.
The separate VST3 host's synthetic state fixture was updated for the additive
velocity-memory field; its exact state-restore comparison still passes.

Earlier regressions covered arrangement and sampler behavior from manual pages
14 and 16, keyboard mapping from page 5, and scrolled lane-control clipping.

The standalone regressions also failed on the earlier processor and passed
with the fixes. They exercise a continuously advancing device clock, pause and
resume timing, Stop followed by Play, and main-output audio after restoring or
changing multi-output routing. A separate VST3 case retains multiple outputs.

Private sample verification established exact decoded PCM for the recovered
resources. That does not establish original preset or effect-output identity.
Effect controls, routing, and behavior have regression coverage, but the
proprietary wet signal path has not been matched exhaustively.

## Still open

Exact wet-preset sound, the Room/Plate send tap relative to Aux3 distortion,
the Multi-mode master-effects flag, native Drive knob angles, whole-interface
visual matching, controlled live-host use and real audio/MIDI devices remain open. A same-source-PCM comparison is
needed to settle the earlier Pitch +2 replay discrepancy. The complete sound
and visual identity requested by the user has not been established.

The retained filter safety cap can differ from the uncapped original below
approximately 40.08 kHz; it is inactive at the tested 44.1/48/96 kHz rates.
Original-payload uploads for fresh reference renders were rejected by automatic
approval review and await explicit approval. The source-only reconstruction
upload subsequently received explicit approval and its Modal validation passed.
The final candidate is staged only; installed copies and the live REAPER session
are unchanged.

Licensing, trial, update, and deactivation services are outside this project's
implementation. Original commercial content and private validation captures
are not distributed here.


## Final checkpoint — 2026-11-03T11:36:55.771618+00:00

The bounded batch 50 private factory run reported 244 patterns from 129 presets, with 0 reported failures. It completed successfully. 12 patterns reached unity; the maximum absolute peak was 1.694586. This checks loading, finite output and expected notes, not original sound parity or absence of clipping.

Matching standalone and VST remain staged in batch-50; installed copies and live REAPER were untouched. Standalone initial and five-slice/flam screenshots were inspected without blank or overlapping controls; exact reference identity remains open. Heavy builds ran on Modal. The local sweep used CPUQuota=150% (1.5 cores of 16), MemoryHigh=1 GiB, MemoryMax=1.5 GiB, TasksMax=32, nice=15 and one numerical-library thread. The recorded scope had no OOM or memory-limit events and the workstation had over 16 GiB available with zero memory PSI. See system-headroom.json and factory-playback-summary.json in batch-50.
