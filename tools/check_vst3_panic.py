#!/usr/bin/env python3
"""Verify MIDI panic reaches the real VST3 processor, using synthetic audio."""
import argparse
import json
from pathlib import Path
import subprocess

import numpy as np
from measure_effect_parity import module_path, native_state, read_pcm24, sha256_file, write_pcm24


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--plugin', type=Path, required=True)
    parser.add_argument('--host', type=Path, required=True)
    parser.add_argument('--artifacts-dir', type=Path, required=True)
    args = parser.parse_args()
    out = args.artifacts_dir.resolve()
    out.mkdir(parents=True, exist_ok=True)
    t = np.arange(24000) / 48000
    write_pcm24(out / 'tone.wav', np.column_stack([0.1 * np.sin(2 * np.pi * 440 * t)] * 2), 48000)
    (out / 'state.json').write_text(json.dumps(native_state(out / 'tone.wav', pad={'oneShot': False})))
    digest = sha256_file(module_path(args.plugin))
    results = {}
    for label, event in [('control', []), ('noteoff', ['--midi', '2400:36:0']),
                         ('cc120', ['--cc', '2400:120:0']), ('cc123', ['--cc', '2400:123:0'])]:
        command = [str(args.host.resolve()), '--plugin', str(args.plugin.resolve()),
                   '--state', str(out / 'state.json'), '--output', str(out / f'{label}.wav'),
                   '--expect-sha256', digest, '--sample-rate', '48000', '--block-size', '64',
                   '--active-frames', '12000', '--midi', '0:36:127', *event]
        result = subprocess.run(command, capture_output=True, text=True, timeout=60)
        (out / f'{label}.log').write_text(result.stdout + result.stderr)
        result.check_returncode()
        _, audio = read_pcm24(out / f'{label}.wav')
        results[label] = {'pre_event_rms': float(np.sqrt(np.mean(audio[1000:2000] ** 2))),
                          'post_event_rms': float(np.sqrt(np.mean(audio[4800:10000] ** 2)))}
    (out / 'results.json').write_text(json.dumps(results, indent=2) + '\n')
    print(json.dumps(results, indent=2))
    assert all(r['pre_event_rms'] > 0.001 for r in results.values()), 'Every case must sound before panic'
    assert results['control']['post_event_rms'] > 0.001, 'Control must still sound after panic time'
    assert all(results[k]['post_event_rms'] == 0 for k in ('noteoff', 'cc120', 'cc123')), 'Note-off and both panic controllers must stop gated playback'


if __name__ == '__main__':
    main()
