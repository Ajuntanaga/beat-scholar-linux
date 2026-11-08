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

## Run the standalone app

After building the JUCE targets, launch the app directly:

```bash
"build/plugin/BeatScholarLinux_artefacts/Release/Standalone/Beat Scholar Linux"
```

Open the cog beside Master for Audio/MIDI Settings, choose an output, and load a
sample or local preset. The app has its own tempo and Play/Pause/Stop controls,
so you can work without a DAW. All pads feed its stereo output, including presets
saved with multiple outputs. Closing the app normally saves the project and
tempo for the next launch.

The [build guide](docs/BUILD.md) also describes the isolated GUI check for
settings and restart recall.

## Where it stands

This is a development snapshot, not a release. The latest recorded staged
candidate is batch 58; later work includes a focused pattern-trigger check and
corrections to the native Init investigation. The working source may include
changes beyond that candidate.

Batch 58 passed ten runnable suites, pluginval at strictness 10, fifteen
actual-VST MIDI cases, and standalone checks. Private factory playback was
skipped in that run. Those results apply to that staged candidate, not every
subsequent source change.

Recent work covers circular slice editing, pattern triggers, Main and Kit Init,
sample playback, effects, state handling, and standalone operation. Exact native
Init samples and hidden defaults remain unresolved. Sound and whole-interface
visual matching are still in progress; passing regressions does not establish
complete compatibility.

See [current status](docs/STATUS.md) for the measurements and limits.

Start with [the reference notes](docs/REFERENCES.md), the tests,
and the source folders:

- `src/core/`: patterns, editing, scheduling, and randomization.
- `src/audio/`: sample playback, mixing, and effects.
- `src/plugin/`: the editor, host integration, and state codec.
- `src/content/`: resource inspection used for local factory compatibility.

If you find a mismatch, a small pattern, the expected result, and the reference
page or clip make it much easier to reproduce. An audio example helps too, if
it can be shared.
