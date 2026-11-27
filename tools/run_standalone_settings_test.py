#!/usr/bin/env python3
"""Check standalone settings and state recall on Xvfb, without audio devices."""
import argparse
import copy
import json
import math
import os
from pathlib import Path
import re
import resource
import shutil
import subprocess
import sys
import tempfile
import time
import xml.etree.ElementTree as ET
import zlib
from PIL import ImageGrab

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('executable', type=Path)
parser.add_argument('output', type=Path)
parser.add_argument('--midi-host', type=Path)
parser.add_argument('--plugin', type=Path)
parser.add_argument('--flam-width', type=int, choices=(960, 1280), default=1280)
args = parser.parse_args()
if (args.midi_host is None) != (args.plugin is None):
    parser.error('--midi-host and --plugin must be supplied together')
executable, output = args.executable, args.output
output.mkdir(parents=True, exist_ok=True)
match = re.fullmatch(r':(\d+)(?:\.\d+)?', os.environ.get('DISPLAY', ''))
if match is None:
    raise RuntimeError('Standalone smoke requires Xvfb')
pid = Path(f'/tmp/.X{match[1]}-lock').read_text().strip()
if Path(f'/proc/{pid}/comm').read_text().strip() != 'Xvfb':
    raise RuntimeError('Refusing to control a desktop display')

# ALSA enumerates physical cards even with a private ALSA_CONFIG_PATH. On a
# workstation that aborts the minimal config's software-device enumeration.
# Hide hardware from this harness and its children; keep the Xvfb socket.
if Path('/dev/snd').exists():
    bubblewrap = shutil.which('bwrap')
    if bubblewrap is None:
        raise RuntimeError('Install bubblewrap to isolate physical audio devices for this test')
    os.execv(bubblewrap, [bubblewrap, '--die-with-parent', '--bind', '/', '/',
                         '--dev', '/dev', '--', sys.executable, __file__, *sys.argv[1:]])

# ALSA's null device never waits for hardware. Real-time priority lets its
# callback starve GUI event handling in a CPU-limited cloud container.
# This limit applies only to this isolated harness and its child processes.
resource.setrlimit(resource.RLIMIT_RTPRIO, (0, 0))

def xdo(*args):
    return subprocess.run(['xdotool', *map(str, args)], text=True,
                          stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=5)

def windows(name, owner, visible=True):
    result = xdo('search', '--all', *(['--onlyvisible'] if visible else []), '--pid', owner, '--name', name)
    return result.stdout.split() if result.returncode == 0 else []

def wait_window(name, owner):
    deadline = time.monotonic() + 45
    while time.monotonic() < deadline:
        found = windows(name, owner)
        if found:
            return found[-1]
        time.sleep(0.1)
    raise RuntimeError(f'Window did not appear: {name}')

def capture_rendered_window(window, filename, expected_size=None):
    # A mapped X11 window can precede its first paint in a busy cloud runner.
    # Require varied pixels inside the content area before saving evidence.
    deadline = time.monotonic() + 15
    while True:
        geometry = dict(line.split('=', 1) for line in
                        xdo('getwindowgeometry', '--shell', window).stdout.splitlines() if '=' in line)
        left, top = int(geometry['X']), int(geometry['Y'])
        width, height = int(geometry['WIDTH']), int(geometry['HEIGHT'])
        frame = ImageGrab.grab(xdisplay=os.environ['DISPLAY'])
        content = frame.crop((left + width // 10, top + height // 5,
                              left + width * 9 // 10, top + height * 9 // 10)).convert('RGB')
        # A resize can update X11 geometry before JUCE paints the new area.
        # In this isolated black Xvfb desktop, both restored outer regions
        # must contain the application, not the previous smaller frame.
        size_painted = expected_size is None or (
            (width, height) == expected_size
            and max(frame.getpixel((left + width - 12, top + height // 2))[:3]) > 20
            and max(frame.getpixel((left + width // 2, top + height - 12))[:3]) > 20)
        if content.getcolors(maxcolors=32) is None and size_painted:
            frame.save(output / filename)
            return
        if time.monotonic() >= deadline:
            frame.save(output / filename)
            raise RuntimeError(f'Window content was blank or unpainted: {filename}')
        time.sleep(0.1)

def record_geometry(stage, geometry):
    # Preserve distinct X11 sizes even if the F2 check fails before its report.
    with (output / 'window-geometry.jsonl').open('a') as trace:
        trace.write(json.dumps({'stage': stage, 'time': time.monotonic(),
                                'geometry': geometry}) + '\n')

def saved_project(home):
    settings = ET.parse(home / '.config' / 'Beat Scholar Linux.settings').getroot()
    encoded = settings.find("VALUE[@name='filterState']").attrib['val']
    # JUCE MemoryBlock uses little-endian groups of six bits, with its own
    # alphabet and a byte-count prefix; it is not RFC 4648 base64.
    length, encoded = encoded.split('.', 1)
    alphabet = '.ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+'
    raw, accumulator, bits = bytearray(), 0, 0
    for char in encoded:
        accumulator |= alphabet.index(char) << bits
        bits += 6
        while bits >= 8:
            raw.append(accumulator & 255)
            accumulator >>= 8
            bits -= 8
    raw = raw[:int(length)]
    if raw[:4] != b'BSLX':
        raise RuntimeError('Saved standalone state has no BSLX envelope')
    return json.loads(zlib.decompress(raw[8:]))

def saved_audio_setup(home):
    settings = ET.parse(home / '.config' / 'Beat Scholar Linux.settings').getroot()
    entry = settings.find("VALUE[@name='audioSetup']")
    if entry is None:
        raise RuntimeError('Explicit audio selection was not saved')
    # JUCE PropertiesFile serializes XML values as child elements.
    setup = entry.find('DEVICESETUP')
    if setup is None:
        raise RuntimeError('Unexpected saved audio setup format')
    return setup.attrib


def close_app(app, main):
    # Click JUCE's own title-bar close button, so the real close/save path
    # also works without a window manager on an isolated Xvfb display.
    geometry = dict(line.split('=', 1) for line in
                    xdo('getwindowgeometry', '--shell', main).stdout.splitlines() if '=' in line)
    xdo('mousemove', '--window', main, int(geometry['WIDTH']) - 16, 15, 'click', '1')
    if app.wait(timeout=10) != 0:
        raise RuntimeError('Standalone did not exit cleanly')

with tempfile.TemporaryDirectory(prefix='bsl-standalone-') as scratch:
    scratch = Path(scratch)
    alsa = scratch / 'asound.conf'
    alsa.write_text('pcm.!default { type null }\n'
                    'pcm.offline { type null hint { show on description "Offline Null" } }\n')
    env = {**os.environ, 'XDG_CONFIG_HOME': str(scratch / 'config'), 'HOME': str(scratch),
           'XDG_DATA_HOME': str(scratch / 'data'), 'XDG_CACHE_HOME': str(scratch / 'cache'),
           'ALSA_CONFIG_PATH': str(alsa), 'JACK_NO_START_SERVER': '1',
           'JACK_DEFAULT_SERVER': f'bsl-offline-{os.getpid()}'}
    with (output / 'application.log').open('w') as log:
        app = None
        try:
            app = subprocess.Popen([str(executable)], env=env, stdout=log, stderr=subprocess.STDOUT)
            main = wait_window('Beat Scholar Linux', app.pid)
            geometry = dict(line.split('=', 1) for line in xdo('getwindowgeometry', '--shell', main).stdout.splitlines() if '=' in line)
            record_geometry('mapped', geometry)
            width, height = int(geometry['WIDTH']), int(geometry['HEIGHT'])
            xdo('windowfocus', '--sync', main)
            capture_rendered_window(main, 'standalone-before.png')
            geometry = dict(line.split('=', 1) for line in
                xdo('getwindowgeometry', '--shell', main).stdout.splitlines() if '=' in line)
            record_geometry('painted', geometry)
            width, height = int(geometry['WIDTH']), int(geometry['HEIGHT'])
            # Manual p5: F2 restores window size after a user resize.
            # X11 measures the JUCE frame too; use the painted default size,
            # not the earlier mapping size before the window has rendered.
            default_window_size = (width, height)
            wrapper_height = round(height - 720 * width / 1280)
            xdo('windowsize', '--sync', main, '960', str(540 + wrapper_height))
            time.sleep(0.2)
            resized = dict(line.split('=', 1) for line in xdo('getwindowgeometry', '--shell', main).stdout.splitlines() if '=' in line)
            record_geometry('resized', resized)
            if int(resized['WIDTH']) != 960:
                raise RuntimeError(f'Window resize precondition failed: {resized}')
            xdo('mousemove', '--window', main, '778', str(wrapper_height + 22), 'click', '1')
            xdo('key', 'F2')
            deadline = time.monotonic() + 5
            last_geometry = None
            while True:
                geometry = dict(line.split('=', 1) for line in xdo('getwindowgeometry', '--shell', main).stdout.splitlines() if '=' in line)
                if geometry != last_geometry:
                    record_geometry('after-f2', geometry)
                    last_geometry = geometry
                width, height = int(geometry['WIDTH']), int(geometry['HEIGHT'])
                if (width, height) == default_window_size:
                    break
                if time.monotonic() >= deadline:
                    raise RuntimeError(f'F2 did not restore default client size: {geometry}')
                time.sleep(0.05)
            capture_rendered_window(main, 'standalone-f2-restored.png', default_window_size)
            scale = width / 1280
            left, top = int(geometry['X']), int(geometry['Y'])
            editor_top = top + height - 720 * scale
            play_bounds = (round(left + 625 * scale), round(editor_top + 16 * scale),
                           round(left + 663 * scale), round(editor_top + 46 * scale))

            def wait_transport(playing, filename):
                deadline = time.monotonic() + 5
                while True:
                    frame = ImageGrab.grab(xdisplay=os.environ['DISPLAY'])
                    icon = frame.crop(play_bounds).convert('RGB')
                    # The playing triangle is green; stopped is neutral grey.
                    active = sum(g > 100 and g > r * 1.5 and g > b * 1.2
                                 for r, g, b in icon.getdata()) > 30
                    if active == playing:
                        frame.save(output / filename)
                        return
                    if time.monotonic() >= deadline:
                        frame.save(output / filename)
                        raise RuntimeError(f'Space did not set the standalone transport to playing={playing}')
                    time.sleep(0.05)

            # A fresh processor starts playing. Establish a stopped state and
            # focus the editor background, as after editing in the tutorial.
            editor_y = height - 720 * scale
            xdo('mousemove', '--window', main, round(573 * scale),
                round(editor_y + 30 * scale), 'click', '1')
            wait_transport(False, 'standalone-stopped.png')
            xdo('mousemove', '--window', main, round(1038 * scale),
                round(editor_y + 30 * scale), 'click', '1')
            xdo('key', 'space')
            wait_transport(True, 'standalone-space-playing.png')
            xdo('key', 'space')
            wait_transport(False, 'standalone-space-paused.png')
            # The default wrapper's title/options area precedes the 1280x720 editor.
            x = round(1250 * width / 1280)
            y = round(height - 720 * width / 1280 + 540 * width / 1280)
            clicked = xdo('mousemove', '--window', main, x, y, 'click', '1')
            if clicked.returncode:
                raise RuntimeError(clicked.stderr)
            settings = wait_window('Audio/MIDI Settings', app.pid)
            capture_rendered_window(settings, 'standalone-settings.png')
            xdo('windowfocus', '--sync', settings)
            # The displayed menu orders Default ALSA Output, Offline Null,
            # then none. Move down from Default to the actual null device.
            xdo('mousemove', '--window', settings, 300, 27, 'click', '1')
            time.sleep(0.2)
            capture_rendered_window(settings, 'standalone-output-menu.png')
            xdo('key', 'Down', 'Return')
            time.sleep(0.2)
            capture_rendered_window(settings, 'standalone-settings-device-changed.png')
            # This dialog belongs to the private ALSA null fixture. Its rate
            # list advances from 44100 to 48000; exercise actual UI selection.
            xdo('mousemove', '--window', settings, 300, 165, 'click', '1')
            xdo('key', 'Down', 'Return')
            time.sleep(0.2)
            capture_rendered_window(settings, 'standalone-settings-rate-changed.png')
            xdo('key', 'Escape')
            deadline = time.monotonic() + 5
            while windows('Audio/MIDI Settings', app.pid) and time.monotonic() < deadline:
                time.sleep(0.1)
            if windows('Audio/MIDI Settings', app.pid):
                raise RuntimeError('Settings dialog did not close with Escape')

            # Exercise the tutorial gesture with real X11 keyboard and pointer
            # events. C++ editor tests separately cover multi-slice attributes.
            xdo('windowfocus', '--sync', main)
            def gesture(*args):
                result = xdo(*args)
                if result.returncode:
                    raise RuntimeError(result.stderr)

            def move_to_beat(center):
                gesture('mousemove', '--window', main, round(center * scale),
                        round(editor_y + 150 * scale))

            def beat_crop(frame, center):
                return frame.crop((round(left + (center - 25) * scale),
                                   round(editor_top + 125 * scale),
                                   round(left + (center + 25) * scale),
                                   round(editor_top + 175 * scale))).convert('RGB')

            def changed_pixels(frame, baseline, center):
                return sum(max(abs(a - b) for a, b in zip(pixel, old)) > 12
                           for pixel, old in zip(beat_crop(frame, center).getdata(),
                                                 beat_crop(baseline, center).getdata()))

            def wait_beats(baseline, centers, changed, filename):
                deadline = time.monotonic() + 5
                while True:
                    frame = ImageGrab.grab(xdisplay=os.environ['DISPLAY'])
                    counts = [changed_pixels(frame, baseline, center) for center in centers]
                    if all(count > 500 if changed else count < 5 for count in counts):
                        frame.save(output / filename)
                        return frame
                    if time.monotonic() >= deadline:
                        frame.save(output / filename)
                        raise RuntimeError(f'Beat gesture {filename} failed: changed pixels {counts}')
                    time.sleep(0.05)

            empty = ImageGrab.grab(xdisplay=os.environ['DISPLAY'])
            move_to_beat(196)
            gesture('click', '1')
            baseline = wait_beats(empty, [196], True, 'standalone-copy-source.png')
            gesture('keydown', 'c')
            gesture('click', '1')
            gesture('keyup', 'c')
            gesture('keydown', 'v')
            move_to_beat(463)
            wait_beats(baseline, [463], True, 'standalone-paste-preview.png')
            gesture('keyup', 'v')
            wait_beats(baseline, [463], False, 'standalone-preview-released.png')
            gesture('keydown', 'v')
            gesture('mousedown', '1')
            for center in (730, 998):
                move_to_beat(center)
                wait_beats(baseline, [center], True, f'standalone-paste-drag-{center}.png')
            gesture('mouseup', '1')
            gesture('keyup', 'v')
            gesture('mousemove', '--window', main, round(1038 * scale),
                    round(editor_y + 30 * scale))
            wait_beats(baseline, [463, 730, 998], True, 'standalone-paste-filled.png')
            gesture('key', 'ctrl+z')
            wait_beats(baseline, [463, 730, 998], False, 'standalone-paste-undone.png')
            gesture('key', 'ctrl+y')
            wait_beats(baseline, [463, 730, 998], True, 'standalone-paste-redone.png')

            # Enter a non-default tempo through the actual editor, then verify
            # the app's own close/reopen persistence (no synthetic state file).
            xdo('windowfocus', '--sync', main)
            tempo_x = round(724 * width / 1280)
            tempo_y = round(height - 720 * width / 1280 + 30 * width / 1280)
            xdo('mousemove', '--window', main, tempo_x, tempo_y,
                'click', '--repeat', '2', '--delay', '100', '1')
            xdo('key', 'ctrl+a')
            xdo('type', '--clearmodifiers', '137')
            xdo('key', 'Return')
            close_app(app, main)
            audio_saved = saved_audio_setup(scratch)
            if audio_saved.get('audioOutputDeviceName') != 'Offline Null':
                raise RuntimeError(f'Output selection did not save Offline Null: {audio_saved}')
            if float(audio_saved.get('audioDeviceRate', 0)) != 48000:
                raise RuntimeError(f'Rate selection did not save 48000 Hz: {audio_saved}')
            saved = saved_project(scratch)
            if saved['global']['beatsPerMinute'] != 137:
                raise RuntimeError('Standalone did not save the edited tempo of 137 BPM')
            beats = saved['patterns'][0]['lanes'][0]['beats']
            if (len(beats) != 4 or len(beats[0]) != 1 or not beats[0][0]['enabled']
                    or any(beat != beats[0] for beat in beats[1:])):
                raise RuntimeError('Actual copy/drag did not persist four matching enabled beats')
            app = subprocess.Popen([str(executable)], env=env, stdout=log, stderr=subprocess.STDOUT)
            main = wait_window('Beat Scholar Linux', app.pid)
            capture_rendered_window(main, 'standalone-restored.png')
            # Inspect the reopened device manager, not only its saved XML.
            xdo('mousemove', '--window', main, x, y, 'click', '1')
            settings = wait_window('Audio/MIDI Settings', app.pid)
            capture_rendered_window(settings, 'standalone-settings-restored.png')
            xdo('windowfocus', '--sync', settings)
            xdo('key', 'Escape')
            deadline = time.monotonic() + 5
            while windows('Audio/MIDI Settings', app.pid) and time.monotonic() < deadline:
                time.sleep(0.1)
            if windows('Audio/MIDI Settings', app.pid):
                raise RuntimeError('Restored settings dialog did not close with Escape')
            close_app(app, main)
            audio_recalled = saved_audio_setup(scratch)
            if audio_recalled != audio_saved:
                raise RuntimeError('Standalone audio setup changed across close/reopen')
            recalled = saved_project(scratch)
            if recalled != saved:
                raise RuntimeError('Standalone project changed across close/reopen')
            # Recreate the tutorial's Ctrl-drag flam with actual X11 input,
            # then inspect the app's own saved project rather than a test model.
            app = subprocess.Popen([str(executable)], env=env, stdout=log, stderr=subprocess.STDOUT)
            main = wait_window('Beat Scholar Linux', app.pid)
            flam_size = (args.flam_width, round(720 * args.flam_width / 1280) + wrapper_height)
            gesture('windowsize', '--sync', main, *flam_size)
            # Leave space above the window for the upward Ctrl-drag.
            gesture('windowmove', '--sync', main, 50, 300)
            capture_rendered_window(main, 'standalone-flam-before.png', flam_size)
            geometry = dict(line.split('=', 1) for line in
                            xdo('getwindowgeometry', '--shell', main).stdout.splitlines() if '=' in line)
            width, height = int(geometry['WIDTH']), int(geometry['HEIGHT'])
            scale = width / 1280
            left, top = int(geometry['X']), int(geometry['Y'])
            editor_y = height - 720 * scale
            editor_top = top + editor_y
            gesture('windowfocus', '--sync', main)
            before_flam = ImageGrab.grab(xdisplay=os.environ['DISPLAY'])
            # Ctrl-drag law (see PluginEditor.cpp dragSubdivisionHeight_): one
            # slice per 0.05 of height*(1/41) normalized, with the row height
            # taken from the live window. Five slices need a drag of
            # 1.951..2.439 row heights; aim at the middle of that band so the
            # result holds at every window width this script uses.
            lane_count = len(saved['patterns'][0]['lanes'])
            cell_h = round(380 * scale) / lane_count
            # Use the current grid's pie centre, including its top gutter.
            # The older centre (196, 150) remains inside the beat, but sector
            # clicks around it can cross a wedge boundary after layout changes.
            pie_x = round(67 * scale) + round(1067 * scale) / (2 * len(beats))
            pie_y = editor_y + round(113 * scale) + cell_h * 10 / 19
            gesture('mousemove', '--window', main, round(pie_x), round(pie_y))
            drag_height = max(1, round(0.8 * (cell_h - round(5 * scale))))
            drag_dy = round(2.195 * drag_height)
            gesture('keydown', 'ctrl', 'mousedown', '1')
            gesture('mousemove', '--window', main, round(pie_x), round(pie_y) - drag_dy)
            time.sleep(0.1)
            gesture('mouseup', '1', 'keyup', 'ctrl')
            # Slice zero begins at six o'clock and runs clockwise.
            for index in (2, 3, 4):
                angle = math.pi / 2 + (index + 0.5) * 2 * math.pi / 5
                gesture('mousemove', '--window', main,
                        round(pie_x + 25 * scale * math.cos(angle)),
                        round(pie_y + 25 * scale * math.sin(angle)), 'click', '1')
            gesture('mousemove', '--window', main, round(1038 * scale),
                    round(editor_y + 30 * scale))
            wait_beats(before_flam, [196], True, 'standalone-five-slice-flam.png')
            close_app(app, main)
            flam = saved_project(scratch)
            (output / 'flam-project.json').write_text(json.dumps(flam, indent=2) + '\n')
            flam_beats = flam['patterns'][0]['lanes'][0]['beats']
            expected_first = [dict(beats[0][0], enabled=index < 2) for index in range(5)]
            if flam_beats[0] != expected_first or flam_beats[1:] != beats[1:]:
                raise RuntimeError('Ctrl-drag flam did not preserve pad/velocity or the other beats')
            midi_result = None
            if args.midi_host is not None:
                from check_vst3_midi_output import canonical, note, rounded
                from measure_effect_parity import module_path, read_pcm24, sha256_file
                # Disable audio only in the isolated host fixture; the saved
                # standalone project above remains the unmodified UI result.
                midi_project = copy.deepcopy(flam)
                midi_project['global'].update(samplerEnabled=False, sequencerEnabled=True)
                preset = output / 'flam-midi-project.json'
                preset.write_text(json.dumps(midi_project))
                digest = sha256_file(module_path(args.plugin))
                rendered = output / 'flam-midi.wav'
                command = [str(args.midi_host), '--plugin', str(args.plugin), '--state', str(preset),
                           '--output', str(rendered), '--expect-sha256', digest, '--sample-rate', '48000',
                           '--block-size', '257', '--active-frames', '24000', '--play-sequencer', '--tempo', '120']
                proc = subprocess.run(command, text=True, capture_output=True, timeout=120, env=env)
                (output / 'flam-midi-host.json').write_text(proc.stdout)
                (output / 'flam-midi-host.log').write_text(proc.stderr)
                if proc.returncode:
                    raise RuntimeError(f'Flam VST3 host exited {proc.returncode}: {proc.stderr[-1000:]}')
                host = json.loads(proc.stdout)
                pitch = flam['pads'][beats[0][0]['padIndex']]['midiNote']
                velocity = rounded(beats[0][0]['velocity'] * 127)
                # Five slices at host 120 BPM: 4800 samples each, with native 98% gates.
                expected_midi = [note(0, 'note_on', pitch, velocity), note(4704, 'note_off', pitch, velocity),
                                 note(4800, 'note_on', pitch, velocity), note(9504, 'note_off', pitch, velocity)]
                if canonical(host['output_midi_events']) != sorted(expected_midi):
                    raise RuntimeError('Actual VST3 MIDI did not match the UI-created two-hit, five-slice flam')
                rate, audio = read_pcm24(rendered)
                if rate != 48000 or audio.any():
                    raise RuntimeError('Sampler-disabled flam produced unexpected audio')
                midi_result = {'pass': True, 'module_sha256': digest, 'host_tempo': 120,
                               'saved_tempo': flam['global']['beatsPerMinute'], 'note_on_samples': [0, 4800]}
            # Real close/relaunch persistence of a loaded, non-Init Randomizer.
            # Isolated fixtures live in the harness's own XDG_DATA_HOME, and the
            # Randomizer is loaded with real pointer clicks on the Randomizer
            # panel's next arrow, so the Standalone holder's own close-button
            # save and constructor reload are the only persistence path used.
            # Original 1.6.2 reference: randomizer-menu-original-20261126/04-randomizer.png
            # (first boot, "Init") and randomizer-default-original-20261126/
            # 04-randomizer.png (later boot, restored Basic Drum Fill + settings).
            random_dir = scratch / 'data' / 'BeatScholarLinux' / 'User' / 'Randomizers'
            random_dir.mkdir(parents=True, exist_ok=True)
            fixtures = {
                'Restart A': dict(selectedPads=[1, 2, 3], selectedSubdivisions=[3, 5],
                                  noteSelectionEnabled=True, slicingEnabled=True,
                                  triggeringEnabled=True, velocityEnabled=True,
                                  fillEmptyOnly=True, triggerChance=0.75, spacing=0.25,
                                  minimumVelocity=0.5, maximumVelocity=0.875),
                'Restart B': dict(selectedPads=[0, 4], selectedSubdivisions=[1, 2],
                                  noteSelectionEnabled=True, slicingEnabled=True,
                                  triggeringEnabled=True, velocityEnabled=True,
                                  fillEmptyOnly=False, triggerChance=0.25, spacing=0.75,
                                  minimumVelocity=0.125, maximumVelocity=0.625),
                'Restart C': dict(selectedPads=[2], selectedSubdivisions=[2, 4],
                                  noteSelectionEnabled=True, slicingEnabled=True,
                                  triggeringEnabled=True, velocityEnabled=True,
                                  fillEmptyOnly=False, triggerChance=0.5, spacing=0.5,
                                  minimumVelocity=0.25, maximumVelocity=0.75)}
            for name, settings in fixtures.items():
                (random_dir / f'{name}.bslrandom').write_text(json.dumps(
                    {'schemaVersion': flam['schemaVersion'], 'randomizer': settings}))
            # The candidate's own arrow order: Factory/Step Randomizer then
            # User/Randomizers, each sorted by full path, ignoring case.
            def arrow_order():
                files = []
                for root in (scratch / 'data' / 'BeatScholarLinux' / 'Factory' / 'Step Randomizer', random_dir):
                    found = [p for p in root.rglob('*') if p.is_file()
                             and p.suffix.lower() in ('.json', '.bslrandom')
                             and p.stem.lower() != 'version']
                    files += sorted(found, key=lambda p: str(p).lower())
                return files
            order = arrow_order()
            if len(order) < 2 or order[0].stem != 'Restart A' or order[1].stem != 'Restart B':
                raise RuntimeError(f'Unexpected Randomizer arrow order for the isolated data root: {order}')

            def launch(first_frame):
                global app
                app = subprocess.Popen([str(executable)], env=env, stdout=log, stderr=subprocess.STDOUT)
                window = wait_window('Beat Scholar Linux', app.pid)
                # Read geometry only after the first paint: the mapped size can precede layout.
                capture_rendered_window(window, first_frame)
                metrics = dict(line.split('=', 1) for line in
                               xdo('getwindowgeometry', '--shell', window).stdout.splitlines() if '=' in line)
                return window, int(metrics['X']), int(metrics['Y']), int(metrics['WIDTH']), int(metrics['HEIGHT'])

            def click_design(window, width, height, cx, cy):
                scale = width / 1280
                gesture('mousemove', '--window', window, round(cx * scale),
                        round(height - 720 * scale + cy * scale), 'click', '1')

            def park(window, width, height):
                scale = width / 1280
                gesture('mousemove', '--window', window, round(1038 * scale),
                        round(height - 720 * scale + 30 * scale))

            def header_crop(frame, left, top, width, height):
                scale = width / 1280
                editor_top = top + height - 720 * scale
                # randomizerTab_ is designRect(546, 524, 360, 30) in PluginEditor.cpp.
                return frame.crop((round(left + 546 * scale), round(editor_top + 524 * scale),
                                   round(left + 906 * scale), round(editor_top + 554 * scale))).convert('RGB')

            def header_difference(first, second):
                return sum(max(abs(a - b) for a, b in zip(p, q)) > 12
                           for p, q in zip(first.getdata(), second.getdata()))

            def same_settings(saved_settings, expected):
                keys = set(expected)
                return (keys <= set(saved_settings) and all(
                    math.isclose(saved_settings[key], value, abs_tol=1e-6)
                    if isinstance(value, float) else saved_settings[key] == value
                    for key, value in expected.items()))

            def open_randomizer_panel(window, width, height, project):
                # The reopened editor restores the panel when the saved UI state
                # says it was open; the first launch has to open it itself.
                if not project['ui'].get('randomizerVisible'):
                    click_design(window, width, height, 1012, 31)
                    time.sleep(0.5)

            # 1. First launch: Init, then load Restart A with the real next arrow.
            main, left, top, width, height = launch('randomizer-restart-first-launch.png')
            gesture('windowfocus', '--sync', main)
            open_randomizer_panel(main, width, height, {'ui': {}})
            park(main, width, height)
            time.sleep(0.3)
            init_frame = ImageGrab.grab(xdisplay=os.environ['DISPLAY'])
            init_header = header_crop(init_frame, left, top, width, height)
            click_design(main, width, height, 1044, 541)
            park(main, width, height)
            time.sleep(0.5)
            capture_rendered_window(main, 'randomizer-restart-loaded.png')
            loaded_frame = ImageGrab.grab(xdisplay=os.environ['DISPLAY'])
            loaded_header = header_crop(loaded_frame, left, top, width, height)
            close_app(app, main)
            first = saved_project(scratch)
            if Path(first['ui'].get('randomizerFile', '')).resolve() != order[0].resolve():
                raise RuntimeError(f"Next arrow from Init did not select Restart A: {first['ui'].get('randomizerFile')}")
            if not same_settings(first['randomizer'], fixtures['Restart A']):
                raise RuntimeError(f"Saved Randomizer settings differ from Restart A: {first['randomizer']}")
            if not first['ui'].get('randomizerVisible'):
                raise RuntimeError('Randomizer panel state was not saved as open')
            if header_difference(init_header, loaded_header) < 100:
                raise RuntimeError('Randomizer header did not change from Init after loading Restart A')

            # 2. Relaunch and close untouched: the whole project, including the
            # Randomizer settings and anchor, must remain equal after decoding the app's own save.
            main, left, top, width, height = launch('randomizer-restart-restored.png')
            gesture('windowfocus', '--sync', main)
            open_randomizer_panel(main, width, height, first)
            park(main, width, height)
            time.sleep(0.5)
            restored_frame = ImageGrab.grab(xdisplay=os.environ['DISPLAY'])
            restored_header = header_crop(restored_frame, left, top, width, height)
            close_app(app, main)
            restored = saved_project(scratch)
            if restored != first:
                raise RuntimeError('Randomizer project changed across close/reopen')
            # Text identity: the restored header reads like Restart A, not Init.
            # (Pixel check; the exact string is covered by testRandomizerSurvivesStandaloneRestart.)
            if header_difference(loaded_header, restored_header) > 40:
                raise RuntimeError('Restored Randomizer header differs from the loaded Restart A header')
            if header_difference(init_header, restored_header) < 100:
                raise RuntimeError('Restored Randomizer header still looks like Init')

            # 3. Relaunch again: the next arrow must continue from the restored
            # anchor (Restart A -> Restart B), not restart from the beginning.
            main, left, top, width, height = launch('randomizer-restart-relaunch-3.png')
            gesture('windowfocus', '--sync', main)
            open_randomizer_panel(main, width, height, restored)
            click_design(main, width, height, 1044, 541)
            park(main, width, height)
            time.sleep(0.5)
            capture_rendered_window(main, 'randomizer-restart-next-after-restore.png')
            close_app(app, main)
            continued = saved_project(scratch)
            if Path(continued['ui'].get('randomizerFile', '')).resolve() != order[1].resolve():
                raise RuntimeError('Next arrow after restart did not continue from the restored anchor '
                                   f"to Restart B: {continued['ui'].get('randomizerFile')}")
            if not same_settings(continued['randomizer'], fixtures['Restart B']):
                raise RuntimeError(f"Saved Randomizer settings differ from Restart B: {continued['randomizer']}")
            if continued['patterns'] != restored['patterns']:
                raise RuntimeError('Randomizer navigation changed the saved pattern data')

            (output / 'result.json').write_text(json.dumps({'settings_opened_from_cog': True,
                'settings_closed_with_escape': True, 'device_selection_changed': True,
                'saved_output_device': audio_saved['audioOutputDeviceName'],
                'sample_rate_changed_via_ui': True, 'audio_setup_recalled': True,
                'saved_sample_rate': float(audio_saved['audioDeviceRate']),
                'standalone_space_play_pause': True, 'f2_restores_window_size': True,
                'actual_c_copy_v_preview_drag_fill': True,
                'single_undo_restores_drag_targets': True, 'redo_restores_drag_targets': True,
                'filled_beats_persisted': True,
                'actual_ctrl_drag_flam_persisted': True, 'flam_width': args.flam_width,
                'flam_vst3_midi': midi_result,
                'randomizer_restart': {'selected_via_real_next_arrow': True,
                                       'settings_and_anchor_recalled': True,
                                       'next_arrow_continues_from_restored_anchor': True,
                                       'restored_name': order[0].stem, 'next_name': order[1].stem},
                'clean_exit': True, 'saved_tempo_bpm': 137, 'project_recalled': True,
                'captures_have_rendered_content': True,
                'main_geometry': geometry, 'click': [x, y]}, indent=2) + '\n')
            print('Standalone settings, Space, C/V preview/drag, undo/redo, restart persistence and Ctrl-drag flam passed.')
        except Exception:
            diagnostic = {'app_exit': None if app is None else app.poll(),
                          'window_search': xdo('search', '--name', '.*').stdout}
            names = []
            for window in diagnostic['window_search'].split():
                names.append({'id': window, 'state': subprocess.run(['xprop', '-id', window, 'WM_STATE', '_NET_WM_STATE'], text=True, capture_output=True).stdout, 'name': xdo('getwindowname', window).stdout.strip(),
                              'pid': xdo('getwindowpid', window).stdout.strip()})
            diagnostic['windows'] = names
            diagnostic['app_pid'] = None if app is None else app.pid
            if app is not None:
                diagnostic['threads'] = {}
                for task in Path(f'/proc/{app.pid}/task').glob('*'):
                    try:
                        diagnostic['threads'][task.name] = {key: (task / key).read_text().strip() for key in ('comm', 'wchan')}
                    except OSError:
                        pass
            (output / 'failure.json').write_text(json.dumps(diagnostic, indent=2) + '\n')
            ImageGrab.grab(xdisplay=os.environ['DISPLAY']).save(output / 'standalone-failure.png')
            raise
        finally:
            for process in (app,):
                if process is not None and process.poll() is None:
                    process.terminate()
                    try:
                        process.wait(timeout=5)
                    except subprocess.TimeoutExpired:
                        process.kill()
                        process.wait(timeout=5)
