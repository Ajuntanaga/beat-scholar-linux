#!/usr/bin/env python3
"""Exercise the standalone cog on Modal's isolated Xvfb display, without devices."""
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile
import time
from PIL import ImageGrab

executable, output = Path(sys.argv[1]), Path(sys.argv[2])
output.mkdir(parents=True, exist_ok=True)
match = re.fullmatch(r':(\d+)(?:\.\d+)?', os.environ.get('DISPLAY', ''))
if match is None:
    raise RuntimeError('Standalone smoke requires Xvfb')
pid = Path(f'/tmp/.X{match[1]}-lock').read_text().strip()
if Path(f'/proc/{pid}/comm').read_text().strip() != 'Xvfb':
    raise RuntimeError('Refusing to control a desktop display')

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

with tempfile.TemporaryDirectory(prefix='bsl-standalone-') as scratch:
    scratch = Path(scratch)
    alsa = scratch / 'asound.conf'
    alsa.write_text('pcm.!default { type null }\npcm.offline { type null hint { show on description "Offline Null" } }\n')
    env = {**os.environ, 'XDG_CONFIG_HOME': str(scratch / 'config'), 'HOME': str(scratch),
           'ALSA_CONFIG_PATH': str(alsa), 'JACK_NO_START_SERVER': '1',
           'JACK_DEFAULT_SERVER': f'bsl-offline-{os.getpid()}'}
    with (output / 'application.log').open('w') as log:
        manager = subprocess.Popen(['openbox'], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        app = None
        try:
            ready = time.monotonic() + 10
            while time.monotonic() < ready:
                wm = subprocess.run(['xprop', '-root', '_NET_SUPPORTING_WM_CHECK'], text=True, capture_output=True)
                if 'window id #' in wm.stdout:
                    break
                if manager.poll() is not None:
                    raise RuntimeError('Openbox exited before WM readiness')
                time.sleep(0.1)
            else:
                raise RuntimeError('Openbox did not publish WM readiness')
            app = subprocess.Popen([str(executable)], env=env, stdout=log, stderr=subprocess.STDOUT)
            main = wait_window('Beat Scholar Linux', app.pid)
            geometry = dict(line.split('=', 1) for line in xdo('getwindowgeometry', '--shell', main).stdout.splitlines() if '=' in line)
            width, height = int(geometry['WIDTH']), int(geometry['HEIGHT'])
            xdo('windowactivate', '--sync', main)
            time.sleep(0.3)
            ImageGrab.grab(xdisplay=os.environ['DISPLAY']).save(output / 'standalone-before.png')
            # The default wrapper's title/options area precedes the 1280x720 editor.
            x = round(1250 * width / 1280)
            y = round(height - 720 * width / 1280 + 540 * width / 1280)
            clicked = xdo('mousemove', '--window', main, x, y, 'click', '1')
            if clicked.returncode:
                raise RuntimeError(clicked.stderr)
            settings = wait_window('Audio/MIDI Settings', app.pid)
            time.sleep(0.2)
            ImageGrab.grab(xdisplay=os.environ['DISPLAY']).save(output / 'standalone-settings.png')
            xdo('windowactivate', '--sync', settings)
            xdo('key', 'Escape')
            deadline = time.monotonic() + 5
            while windows('Audio/MIDI Settings', app.pid) and time.monotonic() < deadline:
                time.sleep(0.1)
            if windows('Audio/MIDI Settings', app.pid):
                raise RuntimeError('Settings dialog did not close with Escape')
            (output / 'result.json').write_text(json.dumps({'settings_opened_from_cog': True,
                'settings_closed_with_escape': True, 'device_selection_changed': False,
                'main_geometry': geometry, 'click': [x, y]}, indent=2) + '\n')
            print('Standalone cog opened Audio/MIDI Settings; Escape closed it; no device selection changed.')
        except Exception:
            diagnostic = {'app_exit': None if app is None else app.poll(),
                          'window_search': xdo('search', '--name', '.*').stdout}
            names = []
            for window in diagnostic['window_search'].split():
                names.append({'id': window, 'state': subprocess.run(['xprop', '-id', window, 'WM_STATE', '_NET_WM_STATE'], text=True, capture_output=True).stdout, 'name': xdo('getwindowname', window).stdout.strip(),
                              'pid': xdo('getwindowpid', window).stdout.strip()})
            diagnostic['windows'] = names
            diagnostic['app_pid'] = None if app is None else app.pid
            diagnostic['manager_exit'] = manager.poll()
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
            for process in (app, manager):
                if process is not None and process.poll() is None:
                    process.terminate()
                    try:
                        process.wait(timeout=5)
                    except subprocess.TimeoutExpired:
                        process.kill()
                        process.wait(timeout=5)
