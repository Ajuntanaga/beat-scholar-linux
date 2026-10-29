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

The full configuration registers nine CTest suites. The private factory playback
suite returns skip code 77 when its content is absent. The remaining tests need
no original samples or presets. GUI tests use Xvfb and avoid the desktop session.

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
