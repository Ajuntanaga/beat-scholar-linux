# Current status

Updated 2026-10-28. This project is staged privately for continued development.
There is no public release or claim of complete compatibility.

## What has been checked

The reference-followup candidate passed a clean Modal build, eight available
CTest suites, and pluginval strictness 10. Those suites include state and editor
checks and a smoke test that loads the built VST3 through a separate host.
The ninth suite needs private factory content and was skipped remotely.

The matching local factory checker passed three presets at 44.1, 48, and 96 kHz,
checking finite, non-silent playback and MIDI activity. A prior candidate passed
initial-pattern playback for 129 factory presets; that wider sweep has not been
repeated on the latest sequencer changes.

The matching standalone passed a local isolated Xvfb settings-open and
Escape-close check using null audio. Its cloud main-window check failed.
Screenshots of the classic seven-slice view, dark view, and local settings
window were inspected. These are specific checks, not whole-interface pixel
comparisons.

The latest candidate is staged separately from the installed plug-in. It has
not been loaded into the user's active REAPER session.

## Repairs already in the source

- Corrected factory sample resource assignments and the pie-sector geometry.
- Fixed event loss in dense patterns and block-size-dependent playback.
- Repaired host-start and seek alignment, pattern triggers, and MIDI boundaries.
- Corrected several undo, automation, routing, sample-history, and state paths.
- Added host drum-note names and the documented dark and MIDI-display options.

Private sample verification established exact decoded PCM for the recovered
resources. That does not establish original preset or effect-output identity.
Effect controls, routing, and behavior have regression coverage, but the
proprietary wet signal path has not been matched exhaustively.

## Still open

Exact sound matching, whole-interface visual matching, and a controlled live-host
test of the latest candidate remain open. The cloud standalone window failure
also needs resolution. Reports should identify the specific reference, behavior,
and tested build rather than calling the reconstruction complete.

Licensing, trial, update, and deactivation services are outside this project's
implementation. Original commercial content and private validation captures
are not distributed here.
