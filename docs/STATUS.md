# Project status — 2026-11-27

Full reference parity is not complete. Behavioral findings and source conflicts
are described in [REFERENCES.md](REFERENCES.md).

## Current validation

Published checkpoint: `83d9b60` (offline dependency licenses in all preset menus).
[GitHub validation run 45975136530](https://github.com/Ajuntanaga/beat-scholar-linux/actions/runs/45975136530)
completed successfully. The previous run on `5233dce` also completed successfully.
The earlier state-roundtrip test-host mismatch has been repaired and published.

Both standalone and VST3 candidates built locally with the latest chooser
changes. Their full isolated editor suite passed in 76.68 seconds. All eight isolated chooser checks passed; inspected screenshots confirm titles and empty save fields. The published
license repair passed the full isolated editor suite in 74.12 seconds.
Factory-asset playback remains distinct from CI checks without those assets.

## Recent changes

- Randomizer probability, persistent decision history, disabled-velocity
  preservation, and silent subdivision growth match the recorded observations.
- MIDI display controls and fresh defaults match desktop 1.6.2 while older
  native saves retain their labels.
- Denominator dragging preserves fractional movement and resets it on pattern
  changes and bar-count changes.

## Remaining limitations

- Fine visual differences remain. Matching individual crops does not establish
  whole-interface pixel identity across sizes.
- Sector-hold behavior and timer cadence are established, but actual host
  repaint jitter and visibility of slices shorter than one tick are not.
- User Data export compatibility and the intended Linux behavior remain
  unresolved; original import behavior is unverified.
- Real audio-device change/reconnect behavior has not been established.
- These checks cover development candidates. Equivalence with currently
  installed binaries has not been established.

### Open Source Libs window (2026-11-27)

Main, kit and randomizer preset menus now open a shared license window. Its
600 x 400 layout follows the captured original; the list and offline notices
cover this build's JUCE, chowdsp_fft and Montserrat dependencies. The original
link action remains unverified. Standalone, VST3 and editor targets built
serially; the isolated editor suite passed in 74.12 seconds, including notice
content, navigation, close/reopen and editor-owned window cleanup. Screenshots
were inspected. An earlier test run received SIGTERM without a result; the
retry passed. Full reference parity remains open.

File-dialog follow-up: all eight main/kit load/save checks passed across isolated standalone and VST3 editor wrappers (titles, user directories, cancellation preserving state/history). Screenshot inspection confirmed blank filename fields in the inspected save dialogs. Native file extensions and operating-system dialog styling remain documented differences.

Enable retention: original factory navigation preserves all four switches. The legacy loader now matches; native recall remains unchanged. Both formats rebuilt and the full isolated editor suite passed in 80.58 seconds, including the retention and chooser regressions.
