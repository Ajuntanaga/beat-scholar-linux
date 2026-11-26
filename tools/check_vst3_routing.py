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
    # Incoming notes use the native fixed map, independently of the editable
    # output midiNote fields in the restored state.
    input_notes = (36, 38, 37, 39, 42, 46, 49, 51, 57, 55, 43, 41, 54, 40, 53, 56)
    renders = {}
    effects = {
        'filter': {'filterEnabled': True, 'cutoff': 0.35, 'resonance': 0.5},
        'crush': {'crush': 0.8},
        'compression': {'compression': 0.8},
    }
    cases = [('stereo', False, {'gain': 1.0}),
             ('multi', True, {'gain': 1.0}),
             ('multi_master_muted', True, {'gain': 0.0})]
    for effect, settings in effects.items():
        cases.extend([(f'stereo_{effect}', False, settings),
                      (f'multi_{effect}', True, settings)])
    bypass_labels = ['multi', 'multi_master_muted',
                     *(f'multi_{effect}' for effect in effects)]
    for label, multi, settings in cases:
        state = native_state(sample, additional_input_wavs=(sample,) * 15,
                             master=settings)
        state['global']['multiOutput'] = multi
        state_path = out / f'{label}.json'
        state_path.write_text(json.dumps(state))
        command = [str(args.host.resolve()), '--plugin', str(args.plugin.resolve()),
                   '--state', str(state_path), '--output', str(out / f'{label}.wav'),
                   '--expect-sha256', digest, '--all-outputs', '--sample-rate', '48000',
                   '--block-size', '64', '--active-frames', '32768', '--pre-roll-frames', '4800']
        for pad, note in enumerate(input_notes):
            command += ['--midi', f'{pad*2048}:{note}:127']
        result = subprocess.run(command, capture_output=True, text=True, timeout=90)
        (out / f'{label}.log').write_text(result.stdout + result.stderr)
        result.check_returncode()
        rate, audio = read_pcm24(out / f'{label}.wav')
        assert rate == 48000 and audio.shape[1] == 34, 'Expected 34 captured channels'
        renders[label] = audio
    # A bypass assertion is meaningful only if each effect is active in Stereo.
    for effect in effects:
        change = float(np.max(np.abs(renders[f'stereo_{effect}'][:, :2]
                                     - renders['stereo'][:, :2])))
        assert change > 1e-4, f'{effect}: inactive Stereo positive control ({change})'
    diagnostics = []
    for pad in range(16):
        window = slice(pad*2048, (pad+1)*2048)
        pair = slice(2+pad*2, 4+pad*2)
        reference = renders['stereo'][window, :2]
        for label in bypass_labels:
            block = renders[label][window]
            difference = block[:, pair] - reference
            diagnostics.append({
                'pad': pad, 'render': label,
                'channel_peaks': np.max(np.abs(block), axis=0).tolist(),
                'reference_peaks': np.max(np.abs(reference), axis=0).tolist(),
                'max_error': float(np.max(np.abs(difference))),
                'max_error_frame_channel': list(map(int, np.unravel_index(
                    np.argmax(np.abs(difference)), difference.shape))),
            })
    (out / 'diagnostics.json').write_text(json.dumps(diagnostics, indent=2) + '\n')
    results = []
    for pad in range(16):
        window = slice(pad*2048, (pad+1)*2048)
        pair = slice(2+pad*2, 4+pad*2)
        reference = renders['stereo'][window, :2]
        assert np.max(np.abs(reference)) > 0.01, f'Pad {pad}: silent reference'
        assert np.max(np.abs(reference[:, 0] - reference[:, 1])) > 0.01, 'Fixture must distinguish L/R'
        assert np.max(np.abs(renders['stereo'][window, 2:])) == 0, 'Stereo leaked to aux'
        for label in bypass_labels:
            block = renders[label][window]
            error = float(np.max(np.abs(block[:, pair] - reference)))
            assert error <= 2/8388608, f'{label} pad {pad}: wrong channel/order/gain {error}'
            others = np.delete(block, [2+pad*2, 3+pad*2], axis=1)
            assert np.max(np.abs(others)) == 0, f'{label} pad {pad}: cross-bus leakage'
        results.append({'pad': pad, 'channels': [2+pad*2, 3+pad*2], 'status': 'pass'})
    (out / 'results.json').write_text(json.dumps(results, indent=2) + '\n')
    print(f'{len(results)}/16 pad routes passed: stereo, multi, master gain/filter/crush/compression bypass')


if __name__ == '__main__':
    main()
