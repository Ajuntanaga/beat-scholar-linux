#!/usr/bin/env python3
"""Check all 16 pad buses through the actual VST3 wrapper."""
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
    sample = out / 'asymmetric.wav'
    t = np.arange(1024) / 48000
    write_pcm24(sample, np.column_stack((0.1 * np.sin(2*np.pi*440*t),
                                         0.07 * np.cos(2*np.pi*733*t))), 48000)
    digest = sha256_file(module_path(args.plugin))
    renders = {}
    for label, multi, gain in [('stereo', False, 1.0), ('multi', True, 1.0),
                               ('multi_master_muted', True, 0.0)]:
        state = native_state(sample, additional_input_wavs=(sample,) * 15,
                             master={'gain': gain})
        state['global']['multiOutput'] = multi
        state_path = out / f'{label}.json'
        state_path.write_text(json.dumps(state))
        command = [str(args.host.resolve()), '--plugin', str(args.plugin.resolve()),
                   '--state', str(state_path), '--output', str(out / f'{label}.wav'),
                   '--expect-sha256', digest, '--all-outputs', '--sample-rate', '48000',
                   '--block-size', '64', '--active-frames', '32768', '--pre-roll-frames', '4800']
        for pad in range(16):
            command += ['--midi', f'{pad*2048}:{state["pads"][pad]["midiNote"]}:127']
        result = subprocess.run(command, capture_output=True, text=True, timeout=90)
        (out / f'{label}.log').write_text(result.stdout + result.stderr)
        result.check_returncode()
        rate, audio = read_pcm24(out / f'{label}.wav')
        assert rate == 48000 and audio.shape[1] == 34, 'Expected 34 captured channels'
        renders[label] = audio
    results = []
    for pad in range(16):
        window = slice(pad*2048, (pad+1)*2048)
        pair = slice(2+pad*2, 4+pad*2)
        reference = renders['stereo'][window, :2]
        assert np.max(np.abs(reference)) > 0.01, f'Pad {pad}: silent reference'
        assert np.max(np.abs(reference[:, 0] - reference[:, 1])) > 0.01, 'Fixture must distinguish L/R'
        assert np.max(np.abs(renders['stereo'][window, 2:])) == 0, 'Stereo leaked to aux'
        for label in ('multi', 'multi_master_muted'):
            block = renders[label][window]
            error = float(np.max(np.abs(block[:, pair] - reference)))
            assert error <= 2/8388608, f'{label} pad {pad}: wrong channel/order/gain {error}'
            others = np.delete(block, [2+pad*2, 3+pad*2], axis=1)
            assert np.max(np.abs(others)) == 0, f'{label} pad {pad}: cross-bus leakage'
        results.append({'pad': pad, 'channels': [2+pad*2, 3+pad*2], 'status': 'pass'})
    (out / 'results.json').write_text(json.dumps(results, indent=2) + '\n')
    print(f'{len(results)}/16 pad routes passed: stereo, multi, and master bypass')


if __name__ == '__main__':
    main()
