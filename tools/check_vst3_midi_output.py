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
                 expected: list[tuple], block: int, tempo: float = TEMPO, *, pre_stop_only: bool = False) -> None:
        case_dir = args.artifacts_dir / f'{name}-{block}'
        case_dir.mkdir(exist_ok=True)
        preset = case_dir / 'state.json'
        preset.write_text(json.dumps(project))
        output = case_dir / 'audio.wav'
        command = [str(args.host), '--plugin', str(args.plugin), '--state', str(preset),
                   '--output', str(output), '--expect-sha256', digest,
                   '--sample-rate', str(SAMPLE_RATE), '--block-size', str(block),
                   '--active-frames', str(active_frames), '--play-sequencer', '--tempo', str(tempo)]
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
            if pre_stop_only:
                actual = [event for event in actual if event[0] < active_frames]
            if actual != sorted(expected):
                errors.append('MIDI events differ from expected sample positions, notes, velocities or gates')
            sample_rate, audio = read_pcm24(output)
            if sample_rate != SAMPLE_RATE or audio.any():
                errors.append('sampler-disabled MIDI mode must output silent audio')
        result = {'name': name, 'block_size': block, 'host_tempo': tempo, 'pass': not errors, 'errors': errors,
                  'expected': sorted(expected), 'actual': canonical(report.get('output_midi_events', [])),
                  'host_report': str(case_dir / 'host.json'), 'pre_stop_only': pre_stop_only}
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
            end = min(active, rounded((quarter + (i + 0.98) / count) * FRAMES_PER_QUARTER))
            pitch = 60 + i % 2
            velocity = rounded((0.25, 0.5, 0.75, 1.0)[i % 4] * 127)
            expected.extend((note(start, 'note_on', pitch, velocity), note(end, 'note_off', pitch, velocity)))
    for block in BLOCK_SIZES:
        # Neither ordinary input notes nor the mapped drum input may leak to MIDI out.
        # Original capture at 48k/140 BPM, blocks 64/257/1024; nearest-frame rounding is
        # incorrect for these odd divisions. The final host-stop release
        # remains a separate expectation because the capture ran longer.
        # Evidence: validation/native-odd-midi-2026-11-13/midi.txt.
        captured_samples = (
            0, 4032, 4114, 8146, 8228, 12260, 12342, 16374, 16457, 20489,
            20571, 23451, 23510, 26390, 26448, 29328, 29387, 32267,
            32326, 35206, 35265, 38145, 38204, 41084, 41142, 43382,
            43428, 45668, 45714, 47954, 48000, 50240, 50285, 52525,
            52571, 54811, 54857, 57097, 57142, 59382, 59428, 61668, 61714,
        )
        assert len(expected) == len(captured_samples) + 1
        block_expected = [(sample, *event[1:])
                          for sample, event in zip(captured_samples, expected)] + [expected[-1]]
        run_case('odd-subdivisions', odd, active,
                 [(1001, 100, 127), (2001, 100, 0), (3041, 60, 64), (4041, 60, 0)], block_expected, block)

    # Original-plugin overlap captures at 48 kHz / 100 BPM / block 64:
    # on0, on0, off7056, off28224 in both lane orders. Independent lanes
    # retain their own releases even when they play the same pad and pitch.
    # Source: validation/native-overlap-2026-11-13/result.json.
    for short_lane in (0, 1):
        shared = copy.deepcopy(state)
        pattern = shared['patterns'][0]
        template = copy.deepcopy(pattern['lanes'][0])
        pattern['lanes'] = []
        for lane_index in (0, 1):
            lane = copy.deepcopy(template)
            lane['beats'] = [beat(4 if lane_index == short_lane else 1)] + [beat(1) for _ in range(3)]
            for quarter, slices in enumerate(lane['beats']):
                for index, hit in enumerate(slices):
                    hit.update(padIndex=0, velocity=0.758928596973419,
                               enabled=quarter == 0 and index == 0)
            pattern['lanes'].append(lane)
        expected = [note(0, 'note_on', 60, 96), note(0, 'note_on', 60, 96),
                    note(7056, 'note_off', 60, 96), note(28224, 'note_off', 60, 96)]
        if short_lane == 0:
            captured_overlap = copy.deepcopy(shared)
        run_case(f'captured-overlap-short-lane-{short_lane}', shared, 40001,
                 [], expected, 64, tempo=100)

    # Exact original same-pad overlap at 140 BPM. Callback PPQ arithmetic
    # makes the short release 5039 at block 257, but 5040 at block 1024.
    # Evidence: validation/native-overlap140-midi-2026-11-13.
    for block, short_end in ((257, 5039), (1024, 5040)):
        captured = copy.deepcopy(captured_overlap)
        for lane in captured['patterns'][0]['lanes']:
            for slices in lane['beats']:
                for hit in slices:
                    hit['velocity'] = 0.75
        run_case('captured-overlap-140', captured, 40001, [],
                 [note(0, 'note_on', 60, 95), note(0, 'note_on', 60, 95),
                  note(short_end, 'note_off', 60, 95), note(20160, 'note_off', 60, 95)],
                 block)

    # Retain the distinct-pad/shared-output-pitch coverage at every host block
    # size. Original explicit output-map captures confirm separate releases
    # at 5039/20160 for block 257 with either pad owning the short note.
    # Evidence: native-trigger-midi-2026-11-13/shared-map-short-pad-{0,1}-257.
    for short_pad in (0, 1):
        shared = copy.deepcopy(state)
        shared['pads'][0]['midiNote'] = shared['pads'][1]['midiNote'] = 60
        pattern = shared['patterns'][0]
        template = copy.deepcopy(pattern['lanes'][0])
        pattern['lanes'] = []
        for pad in (0, 1):
            lane = copy.deepcopy(template)
            lane['beats'] = [beat(4 if pad == short_pad else 1)] + [beat(1) for _ in range(3)]
            for quarter, slices in enumerate(lane['beats']):
                for index, hit in enumerate(slices):
                    hit.update(padIndex=pad, enabled=quarter == 0 and index == 0)
            pattern['lanes'].append(lane)
        expected = [note(0, 'note_on', 60, 95), note(0, 'note_on', 60, 95),
                    note(rounded(0.245 * FRAMES_PER_QUARTER), 'note_off', 60, 95),
                    note(rounded(0.98 * FRAMES_PER_QUARTER), 'note_off', 60, 95)]
        for block in BLOCK_SIZES:
            captured_expected = copy.deepcopy(expected)
            if block == 257:
                captured_expected = [note(0, 'note_on', 60, 95), note(0, 'note_on', 60, 95),
                                     note(5039, 'note_off', 60, 95), note(20160, 'note_off', 60, 95)]
            run_case(f'shared-note-short-pad-{short_pad}', shared, 40001, [], captured_expected, block)

    # Fixed captured timestamps, independent of scheduler arithmetic. The original
    # fixture spans 52 beats, with only the target's first four enabled.
    captures = json.loads((Path(__file__).resolve().parents[1] /
                           'tests/fixtures/native_pattern_triggers.json').read_text())
    for capture in captures['cases']:
        triggered = copy.deepcopy(state)
        target = copy.deepcopy(triggered['patterns'][0])
        target.update(name='Captured target', triggerNote=61,
                      quantization=capture['quantization'])
        target['lanes'][0]['beats'] = [beat(1) for _ in range(4)]
        triggered['patterns'].append(target)
        for pattern in triggered['patterns']:
            pattern['bars'] = [{'numerator': 4, 'denominator': 4} for _ in range(13)]
            for lane in pattern['lanes']:
                lane['beats'].extend([[{'padIndex': 0, 'velocity': 0.75,
                                       'enabled': False, 'velocityAssigned': True}]
                                     for _ in range(52 - len(lane['beats']))])
        expected = [note(sample, kind, 61, velocity)
                    for sample, kind, velocity in capture['expected']]
        run_case('captured-' + capture['name'], triggered, captures['active_frames'],
                 [(capture['trigger_on'], 61, 127), (capture['trigger_off'], 61, 0)],
                 expected, capture.get('block_size', captures['block_size']),
                 captures['tempo'], pre_stop_only=True)

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
            end = min(active, switch + rounded((index + 0.98) * FRAMES_PER_QUARTER))
            expected.extend((note(start, 'note_on', 61, 95), note(end, 'note_off', 61, 95)))
        for block in BLOCK_SIZES:
            block_expected = expected
            if quarters is not None:
                grid = captures['early_grid']
                assert active == grid['active_frames']
                capture = next(c for c in grid['cases']
                               if c['mode'] == quantization and c['block'] == block)
                block_expected = [note(sample, kind, 61, velocity)
                                  for sample, kind, velocity in capture['events']]
                # Reference capture continues playing; independently require the
                # candidate to release its outstanding note when this host stops.
                if block_expected and block_expected[-1][1] == 'note_on':
                    block_expected.append(note(active, 'note_off', 61, 95))
            run_case('pattern-' + quantization, triggered, active,
                     [(1001, 13, 127), (2001, 13, 0)], block_expected, block)

    report = {'status': 'pass' if all(c['pass'] for c in cases) else 'fail',
              'module_sha256': digest, 'host_tempo': TEMPO, 'saved_tempo': 120,
              'sample_rate': SAMPLE_RATE, 'cases': cases,
              'scope': 'Actual VST3 ABI MIDI events and silent sampler-disabled audio; not DAW drag/drop or reference timbre'}
    (args.artifacts_dir / 'report.json').write_text(json.dumps(report, indent=2) + '\n')
    return 0 if report['status'] == 'pass' else 1


if __name__ == '__main__':
    raise SystemExit(main())
