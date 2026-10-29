# Current status

Updated 2026-10-28. This project is staged privately for continued development.
There is no public release or claim of complete compatibility.

## What has been checked

The latest candidate passed a clean Modal build, eight available CTest suites,
and pluginval strictness 10. Those suites include state and editor checks and a
smoke test that loads the built VST3 through a separate host. The ninth suite
needs private factory content and was skipped remotely.

The matching local checker passed four-second initial-pattern playback for all
129 private factory presets at 48 kHz. It also passed three presets at 44.1, 48,
and 96 kHz. The checks cover loading, finite audio, expected non-silent playback,
and MIDI activity. They do not compare the output with the original plug-in.
Of the 129 presets, 87 produced samples at or above full scale; the largest
absolute peak was 3.261248. Those measurements need reference comparison before
calling the presets sonically correct or changing their gain.

The matching standalone passed a local isolated Xvfb settings-open and
Escape-close check using null audio. Its cloud main-window check still failed.
Arrangement views were inspected at widths of 960, 1280, and 1920 pixels,
alongside zoomed/scrolled, classic seven-slice, dark, custom-sample, and local
settings views. These checks cover specific states; whole-interface pixel
identity has not been established.

The latest candidate is staged separately from the installed plug-in. It has
not been loaded into the user's active REAPER session.

## Repairs already in the source

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

New regression tests reproduced the editing and import defects on the earlier
source and passed after the repairs on Modal. Screenshot inspection then found
the lane-control clipping defect; its pixel regression passes in the final
candidate. The arrangement and sampler checks use manual pages 14 and 16, and
the keyboard mapping follows page 5.

Private sample verification established exact decoded PCM for the recovered
resources. That does not establish original preset or effect-output identity.
Effect controls, routing, and behavior have regression coverage, but the
proprietary wet signal path has not been matched exhaustively.

## Still open

Exact sound matching, the full-scale preset measurements, whole-interface visual
matching, and a controlled live-host test of the latest candidate remain open.
The cloud standalone window failure also needs resolution. Reports should name
the specific reference, behavior, and tested build.

Licensing, trial, update, and deactivation services are outside this project's
implementation. Original commercial content and private validation captures
are not distributed here.
