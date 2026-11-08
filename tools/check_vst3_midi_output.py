#!/usr/bin/env python3
"""Exercise tutorial MIDI workflows through the built VST3 ABI, using synthetic audio."""
from __future__ import annotations

import argparse
import copy
import json
import math
from pathlib import Path
import subprocess

from measure_effect_parity import (module_path, native_state, read_pcm24,
                                   sha256_file, write_impulse_pcm24)

SAMPLE_RATE = 48000
TEMPO = 140
FRAMES_PER_QUARTER = SAMPLE_RATE * 60 / TEMPO
BLOCK_SIZES = (64, 257, 1024)


def rounded(value: float) -> int:
    return math.floor(value + 0.5)


def note(sample: int, kind: str, pitch: int, velocity: int = 0) -> tuple:
    return sample, kind, 1, pitch, velocity


def canonical(events: list[dict]) -> list[tuple]:
    return sorted((e['sample'], e['type'], e['channel'], e.get('note'), e.get('velocity'))
                  for e in events)


def beat(count: int, *, alternating: bool = False) -> list[dict]:
    return [{'padIndex': i % 2 if alternating else 1,
             'velocity': (0.25, 0.5, 0.75, 1.0)[i % 4] if alternating else 0.75,
             'enabled': True, 'velocityAssigned': True} for i in range(count)]


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--plugin', type=Path, required=True)
    parser.add_argument('--host', type=Path, required=True)
    parser.add_argument('--artifacts-dir', type=Path, required=True)
    args = parser.parse_args()
    args.artifacts_dir.mkdir(parents=True, exist_ok=True)
    impulse = args.artifacts_dir / 'synthetic-impulse.wav'
    write_impulse_pcm24(impulse)
    state = native_state(impulse)
    state['global'].update(sequencerEnabled=True, samplerEnabled=False)
    # The saved standalone tempo deliberately disagrees with the host.
    state['pads'][0]['midiNote'] = 60
    state['pads'][1]['midiNote'] = 61
    digest = sha256_file(module_path(args.plugin))
    cases = []

    def run_case(name: str, project: dict, active_frames: int, inputs: list[tuple],
                 expected: list[tuple], block: int) -> None:
        case_dir = args.artifacts_dir / f'{name}-{block}'
        case_dir.mkdir(exist_ok=True)
        preset = case_dir / 'state.json'
        preset.write_text(json.dumps(project))
        output = case_dir / 'audio.wav'
        command = [str(args.host), '--plugin', str(args.plugin), '--state', str(preset),
                   '--output', str(output), '--expect-sha256', digest,
                   '--sample-rate', str(SAMPLE_RATE), '--block-size', str(block),
                   '--active-frames', str(active_frames), '--play-sequencer', '--tempo', str(TEMPO)]
        for event in inputs:
            command.extend(('--midi', ':'.join(map(str, event))))
        proc = subprocess.run(command, text=True, capture_output=True, timeout=120)
        (case_dir / 'stderr.log').write_text(proc.stderr)
        (case_dir / 'host.json').write_text(proc.stdout)
        errors = []
        report = {}
        if proc.returncode:
            errors.append(f'host exited {proc.returncode}: {proc.stderr[-1000:]}')
        else:
            report = json.loads(proc.stdout)
            actual = canonical(report['output_midi_events'])
            if actual != sorted(expected):
                errors.append('MIDI events differ from expected sample positions, notes, velocities or gates')
            sample_rate, audio = read_pcm24(output)
            if sample_rate != SAMPLE_RATE or audio.any():
                errors.append('sampler-disabled MIDI mode must output silent audio')
        result = {'name': name, 'block_size': block, 'pass': not errors, 'errors': errors,
                  'expected': sorted(expected), 'actual': canonical(report.get('output_midi_events', [])),
                  'host_report': str(case_dir / 'host.json')}
        cases.append(result)
        print(f"{name}/{block}: {'PASS' if not errors else 'FAIL: ' + '; '.join(errors)}", flush=True)

    # Odd subdivisions, ghost velocities and a stop during the final long gate.
    subdivisions = (5, 7, 9, 1)
    odd = copy.deepcopy(state)
    odd['patterns'][0]['lanes'][0]['beats'] = [beat(n, alternating=True) for n in subdivisions]
    active = 70001
    expected = []
    for quarter, count in enumerate(subdivisions):
        for i in range(count):
            start = rounded((quarter + i / count) * FRAMES_PER_QUARTER)
            end = min(active, rounded((quarter + (i + 1) / count) * FRAMES_PER_QUARTER))
            pitch = 60 + i % 2
            velocity = rounded((0.25, 0.5, 0.75, 1.0)[i % 4] * 127)
            expected.extend((note(start, 'note_on', pitch, velocity), note(end, 'note_off', pitch)))
    for block in BLOCK_SIZES:
        # Neither ordinary input notes nor the mapped drum input may leak to MIDI out.
        run_case('odd-subdivisions', odd, active,
                 [(1001, 100, 127), (2001, 100, 0), (3041, 60, 64), (4041, 60, 0)], expected, block)

    # Start from a silent pattern and switch via its separate pattern-note mapping.
    for quantization, quarters in [('off', None), ('quarter', 1), ('sixteenth', 0.25),
                                   ('thirtySecond', 0.125)]:
        triggered = copy.deepcopy(state)
        target = copy.deepcopy(triggered['patterns'][0])
        target.update(name='Triggered', triggerNote=13, quantization=quantization)
        target['lanes'][0]['beats'] = [beat(1) for _ in range(4)]
        triggered['patterns'].append(target)
        switch = 1001 if quarters is None else rounded(quarters * FRAMES_PER_QUARTER)
        expected = []
        for index in range(4):
            start = switch + rounded(index * FRAMES_PER_QUARTER)
            if start >= active:
                break
            end = min(active, switch + rounded((index + 1) * FRAMES_PER_QUARTER))
            expected.extend((note(start, 'note_on', 61, 95), note(end, 'note_off', 61)))
        for block in BLOCK_SIZES:
            run_case('pattern-' + quantization, triggered, active,
                     [(1001, 13, 127), (2001, 13, 0)], expected, block)

    report = {'status': 'pass' if all(c['pass'] for c in cases) else 'fail',
              'module_sha256': digest, 'host_tempo': TEMPO, 'saved_tempo': 120,
              'sample_rate': SAMPLE_RATE, 'cases': cases,
              'scope': 'Actual VST3 ABI MIDI events and silent sampler-disabled audio; not DAW drag/drop or reference timbre'}
    (args.artifacts_dir / 'report.json').write_text(json.dumps(report, indent=2) + '\n')
    return 0 if report['status'] == 'pass' else 1


if __name__ == '__main__':
    raise SystemExit(main())
