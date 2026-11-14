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

def capture_rendered_window(window, filename):
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
        if content.getcolors(maxcolors=32) is None:
            frame.save(output / filename)
            return
        if time.monotonic() >= deadline:
            frame.save(output / filename)
            raise RuntimeError(f'Window content was blank or unpainted: {filename}')
        time.sleep(0.1)

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
    alsa.write_text('pcm.!default { type null }\npcm.offline { type null hint { show on description "Offline Null" } }\n')
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
            width, height = int(geometry['WIDTH']), int(geometry['HEIGHT'])
            xdo('windowfocus', '--sync', main)
            capture_rendered_window(main, 'standalone-before.png')
            scale = width / 1280
            left, top = int(geometry['X']), int(geometry['Y'])
            editor_top = top + height - 720 * scale
            play_bounds = (round(left + 539 * scale), round(editor_top + 16 * scale),
                           round(left + 569 * scale), round(editor_top + 46 * scale))

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
            xdo('mousemove', '--window', main, round(510 * scale),
                round(editor_y + 30 * scale), 'click', '1')
            wait_transport(False, 'standalone-stopped.png')
            xdo('mousemove', '--window', main, round(1000 * scale),
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
            gesture('mousemove', '--window', main, round(1000 * scale),
                    round(editor_y + 30 * scale))
            wait_beats(baseline, [463, 730, 998], True, 'standalone-paste-filled.png')
            gesture('key', 'ctrl+z')
            wait_beats(baseline, [463, 730, 998], False, 'standalone-paste-undone.png')
            gesture('key', 'ctrl+y')
            wait_beats(baseline, [463, 730, 998], True, 'standalone-paste-redone.png')

            # Enter a non-default tempo through the actual editor, then verify
            # the app's own close/reopen persistence (no synthetic state file).
            xdo('windowfocus', '--sync', main)
            tempo_x = round(625 * width / 1280)
            tempo_y = round(height - 720 * width / 1280 + 30 * width / 1280)
            xdo('mousemove', '--window', main, tempo_x, tempo_y,
                'click', '--repeat', '2', '--delay', '100', '1')
            xdo('key', 'ctrl+a')
            xdo('type', '--clearmodifiers', '137')
            xdo('key', 'Return')
            close_app(app, main)
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
            close_app(app, main)
            recalled = saved_project(scratch)
            if recalled != saved:
                raise RuntimeError('Standalone project changed across close/reopen')
            # Recreate the tutorial's Ctrl-drag flam with actual X11 input,
            # then inspect the app's own saved project rather than a test model.
            app = subprocess.Popen([str(executable)], env=env, stdout=log, stderr=subprocess.STDOUT)
            main = wait_window('Beat Scholar Linux', app.pid)
            capture_rendered_window(main, 'standalone-flam-before.png')
            geometry = dict(line.split('=', 1) for line in
                            xdo('getwindowgeometry', '--shell', main).stdout.splitlines() if '=' in line)
            width, height = int(geometry['WIDTH']), int(geometry['HEIGHT'])
            scale = width / 1280
            left, top = int(geometry['X']), int(geometry['Y'])
            editor_y = height - 720 * scale
            editor_top = top + editor_y
            gesture('windowfocus', '--sync', main)
            before_flam = ImageGrab.grab(xdisplay=os.environ['DISPLAY'])
            move_to_beat(196)
            gesture('keydown', 'ctrl', 'mousedown', '1')
            # Subdivision drag sensitivity is in editor pixels, not design units.
            gesture('mousemove', '--window', main, round(196 * scale),
                    round(editor_y + 150 * scale) - 48)
            time.sleep(0.1)
            gesture('mouseup', '1', 'keyup', 'ctrl')
            # Slice zero begins at six o'clock and runs clockwise.
            for index in (2, 3, 4):
                angle = math.pi / 2 + (index + 0.5) * 2 * math.pi / 5
                gesture('mousemove', '--window', main,
                        round((196 + 25 * math.cos(angle)) * scale),
                        round(editor_y + (150 + 25 * math.sin(angle)) * scale), 'click', '1')
            gesture('mousemove', '--window', main, round(1000 * scale),
                    round(editor_y + 30 * scale))
            wait_beats(before_flam, [196], True, 'standalone-five-slice-flam.png')
            close_app(app, main)
            flam = saved_project(scratch)
            flam_beats = flam['patterns'][0]['lanes'][0]['beats']
            expected_first = [dict(beats[0][0], enabled=index < 2) for index in range(5)]
            if flam_beats[0] != expected_first or flam_beats[1:] != beats[1:]:
                raise RuntimeError('Ctrl-drag flam did not preserve pad/velocity or the other beats')
            (output / 'flam-project.json').write_text(json.dumps(flam, indent=2) + '\n')
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
                expected_midi = [note(0, 'note_on', pitch, velocity), note(4704, 'note_off', pitch),
                                 note(4800, 'note_on', pitch, velocity), note(9504, 'note_off', pitch)]
                if canonical(host['output_midi_events']) != sorted(expected_midi):
                    raise RuntimeError('Actual VST3 MIDI did not match the UI-created two-hit, five-slice flam')
                rate, audio = read_pcm24(rendered)
                if rate != 48000 or audio.any():
                    raise RuntimeError('Sampler-disabled flam produced unexpected audio')
                midi_result = {'pass': True, 'module_sha256': digest, 'host_tempo': 120,
                               'saved_tempo': flam['global']['beatsPerMinute'], 'note_on_samples': [0, 4800]}
            (output / 'result.json').write_text(json.dumps({'settings_opened_from_cog': True,
                'settings_closed_with_escape': True, 'device_selection_changed': False,
                'standalone_space_play_pause': True,
                'actual_c_copy_v_preview_drag_fill': True,
                'single_undo_restores_drag_targets': True, 'redo_restores_drag_targets': True,
                'filled_beats_persisted': True,
                'actual_ctrl_drag_flam_persisted': True, 'flam_vst3_midi': midi_result,
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
