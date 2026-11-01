# Current status

Updated 2026-10-31. This project is staged privately for continued development.
There is no public release or claim of complete compatibility.

## What has been checked

The latest candidate passed a clean Modal build, eight available CTest suites,
and pluginval strictness 10. Those suites include state and editor checks and a
smoke test that loads the built VST3 through a separate host. The ninth suite
needs private factory content and was skipped remotely.

The matching local checker passed four-second initial-pattern playback for
three private factory presets at 44.1, 48, and 96 kHz. The checks cover loading,
finite audio, expected non-silent playback, and MIDI activity. They do not
compare the output with the original plug-in.

The previous candidate passed all 129 private factory presets at 48 kHz. Of
those, 87 produced samples at or above full scale; the largest absolute peak was
3.261248. The current three-preset check also reaches full scale. These
measurements need reference comparison before calling the presets sonically
correct or changing their gain. The 129-preset sweep was not repeated for the
editing and state-import changes.

The matching standalone passed a local isolated Xvfb check using null audio:
settings opened from the cog and closed with Escape, the app saved an edited
137 BPM tempo, and the saved project matched after a clean close and reopen.
The settings and restored-editor screenshots were inspected. Physical audio and
MIDI devices were not tested. The cloud main-window check still timed out.

The current candidate also passed seven local standalone interaction captures:
default, randomizer, seven-slice editing, beat and lane context menus, and compact
randomizer and sampler views. The saved state confirms the held-key gesture made
seven slices. The inspected compact view now shows the full Trigger Chance label.

The previous audit inspected arrangement views at widths of 960, 1280, and
1920 pixels, alongside zoomed/scrolled, classic seven-slice, dark, custom-sample,
and local settings views. These checks cover specific states; whole-interface
pixel identity has not been established.

The latest candidate is staged separately from the installed plug-in. It has
not been loaded into the user's active REAPER session.

## Repairs already in the source

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
- Corrected factory sample resource assignments and the pie-sector geometry.
- Fixed event loss in dense patterns and block-size-dependent playback.
- Repaired host-start and seek alignment, pattern triggers, and MIDI boundaries.
- Corrected several undo, automation, routing, sample-history, and state paths.
- Added host drum-note names and the documented dark and MIDI-display options.
- Subdivision and velocity drags publish each change to playback, with one Undo
  step per changed drag and none for an unchanged click.
- MIDI Note Off releases its matching held voice without cutting overlapping
  notes, sequenced hits, or pad previews. Stolen and finished voices keep their
  release pairing.
- Muted pads and pads excluded by Solo no longer leak their Room/Reverb returns
  or light the pad meter. Their delay buffers keep advancing while inaudible.
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

The current core, state, and editor regressions reproduced the meter, velocity,
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

Exact sound matching, the full-scale preset measurements, whole-interface visual
matching, a controlled live-host test, and real audio/MIDI-device use of the
latest candidate remain open. The cloud standalone window failure also needs
resolution. Reports should name the specific reference, behavior, and tested
build.

Licensing, trial, update, and deactivation services are outside this project's
implementation. Original commercial content and private validation captures
are not distributed here.
