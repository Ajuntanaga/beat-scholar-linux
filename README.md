# Beat Scholar Linux

A native Linux drum sequencer and sampler, built around Beat Scholar's circular
beat slices and flexible rhythm editing. It builds as a VST3 instrument and a
standalone application, using C++20 and JUCE.

This is an independent reconstruction by Ajuntanaga, being developed
in a private repository. It is not an official Modalics product. The manual,
official visuals, and release notes guide the implementation; work on the
original sound and interface is still in progress.

## What is here

The source includes a 16-pad sampler, up to 16 sequencer lanes, and beats with
up to 45 slices. There is also pattern switching, swing, per-slice editing,
randomization, MIDI input and output, MIDI export, sample loading, effects,
multi-output routing, and saved plug-in state. You can use the sequencer with
external instruments or play the sampler from incoming MIDI.

The core rhythm and audio code is separate from the JUCE editor and host layer.
Many timing, audio, and state regressions can be tested without opening a
DAW. Editor checks run on an isolated display, and a separate VST3 host loads
the actual built bundle.

Private factory samples, presets, original binaries, and license files are not
included. Synthetic fixtures support the portable tests. The plug-in can load
your own samples; a fresh clone does not come with the original factory library.

## Try the core

You need CMake 3.25 or newer, a C++20 compiler, and Python 3.10 or newer with
NumPy installed. From the repository root:

```bash
cmake --preset debug
cmake --build --preset debug --parallel 2
ctest --preset debug
```

This builds the core and its tests without JUCE or a DAW. For the VST3 and
standalone application, follow [the build guide](docs/BUILD.md), including the
pinned JUCE checkout and Linux development packages. Builds stay in the build
directory; they do not install or replace a plug-in in your host.

## Where it stands

The latest validated candidate passed the available source-only CTest suites,
an actual VST3 host smoke test, and pluginval at strictness 10. Local checks with
private content also exercised three presets at 44.1, 48, and 96 kHz. These are
regression checks, not proof that every preset sounds identical to the original.

Recent fixes cover incorrect sample assignments, pie-slice geometry, dropped
events in dense patterns, transport alignment, and state handling. The latest
pass also fixes beat-drag updates and Undo, overlapping MIDI note releases,
muted effect tails, and sampler dividers. Full wet-effect waveform matching and
exhaustive visual matching remain open. The standalone settings check passes locally on an isolated display;
the cloud window check still fails. See [current status](docs/STATUS.md) for the
scope and limits of those results.

Start with [the reference notes](docs/REFERENCES.md), the tests,
and the source folders:

- `src/core/`: patterns, editing, scheduling, and randomization.
- `src/audio/`: sample playback, mixing, and effects.
- `src/plugin/`: the editor, host integration, and state codec.
- `src/content/`: resource inspection used for local factory compatibility.

If you find a mismatch, a small pattern, the expected result, and the reference
page or clip make it much easier to reproduce. An audio example helps too, if
it can be shared.
