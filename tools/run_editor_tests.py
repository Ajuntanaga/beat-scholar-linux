#!/usr/bin/env python3
"""Run real JUCE focus tests on an isolated DISPLAY without WM shortcut grabs."""
import os
from pathlib import Path
import re
import signal
import subprocess
import sys
import time

# Never inject keys or start a test window manager on the user's desktop.
match = re.fullmatch(r":(\d+)(?:\.\d+)?", os.environ.get("DISPLAY", ""))
if match is None:
    raise RuntimeError("Editor tests require an isolated local Xvfb display")
server_pid = Path(f"/tmp/.X{match[1]}-lock").read_text().strip()
if Path(f"/proc/{server_pid}/comm").read_text().strip() != "Xvfb":
    raise RuntimeError("Refusing editor test input outside Xvfb")

with subprocess.Popen(["openbox"], stdout=subprocess.DEVNULL,
                      stderr=subprocess.DEVNULL) as window_manager:
    try:
        time.sleep(0.5)
        if window_manager.poll() is not None:
            raise RuntimeError("openbox failed to start on the test display")
        # Seed JUCE's required EWMH atoms, then remove global Alt/arrow grabs.
        # Xvfb -noreset retains the atoms when the window manager disconnects.
        window_manager.terminate()
        window_manager.wait(timeout=5)
        # Full audio/editor fixtures can approach 90 seconds under host load.
        # Individual input and menu readiness deadlines remain bounded in C++.
        result = subprocess.run(sys.argv[1:], timeout=180,
                                env={**os.environ, "BSL_ISOLATED_GUI_TESTS": "1"})
    finally:
        if window_manager.poll() is None:
            window_manager.terminate()
            window_manager.wait(timeout=5)
if result.returncode < 0:
    print("Editor test terminated by " + signal.Signals(-result.returncode).name, file=sys.stderr)
sys.exit(result.returncode)
