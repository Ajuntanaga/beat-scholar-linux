# Project status — 2026-11-27

Full reference parity is not complete. Behavioral findings and source conflicts
are described in [REFERENCES.md](REFERENCES.md).

## Current validation

Published checkpoint: `cad44ae16f7a821037636788e11624b96f297319`.
[GitHub validation run 45967730325](https://github.com/Ajuntanaga/beat-scholar-linux/actions/runs/45967730325)
built successfully but failed four actual-VST3 checks at their shared
state-roundtrip assertion. The test host expected omitted MIDI display keys,
while the codec now writes their historical migration defaults explicitly.

The local test-host repair fills only missing expectations; supplied values
remain strictly compared. The failure was reproduced before the repair, and
all four affected checks passed afterward. This repair awaits publication and
a new remote run. Remote Gain/Dist, host-gate timing, strictness-10 plugin
validation and standalone settings/recall checks passed. Factory playback was
skipped because the CI environment did not contain factory assets.

Both standalone and VST3 candidates built locally. The full local editor
regression passed, including the added denominator-reset sequence after adding
a bar. That added regression also awaits publication.

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
