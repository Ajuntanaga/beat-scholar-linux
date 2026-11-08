# Building on Linux

Run these commands from the repository root. The core needs CMake 3.25 or newer,
a C++20 compiler, and Python 3.10 or newer. The plug-in also needs JUCE and Linux
audio, font, OpenGL, and X11 development packages.

On Debian or Ubuntu, the package names used by the cloud build are:

```text
build-essential cmake ninja-build pkg-config
libasound2-dev libfontconfig1-dev libfreetype6-dev libgl1-mesa-dev libgtk-3-dev
libx11-dev libxcomposite-dev libxcursor-dev libxext-dev libxinerama-dev
libxrandr-dev libxrender-dev
```

The GUI tests also use these packages:

```text
xvfb openbox xauth xdotool x11-utils fonts-lato fonts-urw-base35
```

Create a Python environment for the test helpers:

```bash
python3 -m venv .venv
. .venv/bin/activate
python -m pip install numpy==2.2.6 Pillow==11.3.0
```

## Core and tests

```bash
cmake --preset debug
cmake --build --preset debug --parallel 2
ctest --preset debug
```

These five suites cover the core, audio engine, scheduler, factory migration,
and effect-comparison helpers. They use generated or synthetic content.

## VST3 and standalone

The tested JUCE version is
[8.0.16-security](https://github.com/juce-framework/JUCE/releases/tag/8.0.16-security),
commit `ea77444750de45e036e0279bb0291144bf8e3688`. Fetch it separately:

```bash
git clone --depth 1 --branch 8.0.16-security https://github.com/juce-framework/JUCE.git third_party/JUCE
cmake -S . -B build/plugin -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DBSL_BUILD_TESTS=ON -DBSL_BUILD_PLUGIN=ON
cmake --build build/plugin --parallel 2
ctest --test-dir build/plugin --output-on-failure
```

The outputs are:

```text
build/plugin/BeatScholarLinux_artefacts/Release/VST3/Beat Scholar Linux.vst3
build/plugin/BeatScholarLinux_artefacts/Release/Standalone/Beat Scholar Linux
```

The build does not install them automatically. Test a new bundle in a separate
host session before replacing one used by an open project.

The full configuration registers core, processor, editor and actual-VST checks;
the available suites depend on the enabled targets and installed tools. The
private factory playback suite returns skip code 77 when its content is absent. The remaining tests need
no original samples or presets. GUI tests use Xvfb and avoid the desktop session.

## Using and checking the standalone app

Launch the executable without a host:

```bash
"build/plugin/BeatScholarLinux_artefacts/Release/Standalone/Beat Scholar Linux"
```

Use the cog beside Master to open Audio/MIDI Settings. Choose your output device,
sample rate, buffer size, and any MIDI inputs or output you want to use. Load a
sample or local preset, set the tempo, and use the editor's Play/Pause/Stop
controls. Pause keeps the current position; Stop returns to the beginning.

The standalone app mixes all pads to stereo, including projects saved with
multiple outputs. The VST3 retains its multiple-output routing and follows the
host's transport. On a normal close, the app saves its project and tempo; JUCE
also stores the audio-device setup for the next launch.

For a GUI check that avoids your desktop, settings, and audio devices, run:

```bash
xvfb-run -a -s '-screen 0 1600x1200x24' python tools/run_standalone_settings_test.py \
  "build/plugin/BeatScholarLinux_artefacts/Release/Standalone/Beat Scholar Linux" \
  /tmp/beat-scholar-standalone-check
```

The helper uses a temporary home directory, an isolated Xvfb display, and null
audio. It opens settings from the cog, closes them with Escape, enters 137 BPM,
then closes and reopens the app and compares the saved project. It writes
screenshots, a log, and a JSON result to the output directory. It disables
real-time priority only for the test process and its children. It does not
select or validate a physical audio or MIDI device; that needs a separate check.

## Local content

The factory lookup uses an absolute `XDG_DATA_HOME` when set, otherwise
`~/.local/share`, followed by `BeatScholarLinux/Factory`. The source repository
does not provide that library. Custom samples can be loaded through the editor.

For an existing licensed local data library, the migration helper accepts an
explicit source and supports a dry run:

```bash
python tools/migrate_factory_content.py --source /path/to/BeatScholarData --dry-run
```

It copies preset, kit, and step-randomizer JSON when run without `--dry-run`;
it does not migrate license files or extract audio. Keep migrated content local.

## Optional cloud validation

`tools/modal_audit.py` runs a source-only build on Modal using 16 CPUs and
49,152 MiB of memory. It requires a configured Modal account, the JUCE checkout,
and a Linux pluginval executable at `tools/bin/pluginval`. It uploads the
allowlisted tools, source, tests, JUCE, and pluginval; private factory content
stays local. Running it incurs Modal usage charges.

```bash
modal run tools/modal_audit.py --run-id my-unique-audit-id
```

Choose a new run ID each time. Results and candidate archives go to the Modal
artifact volume. The standalone cloud window check has a known failure; read
the returned exit codes rather than treating artifact creation as a pass.
