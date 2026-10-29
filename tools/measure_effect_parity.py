#!/usr/bin/env python3
"""Measure effects by rendering the built VST3 through the headless ABI host.

No reconstruction transfer is implemented in Python. Every candidate WAV in
this report is produced by ``actual_vst3_host`` from the exact bundle supplied
on the command line.
"""

from __future__ import annotations

import argparse
import base64
import hashlib
import json
import math
import os
import subprocess
import tempfile
import wave
from pathlib import Path
from typing import Any, Callable

import numpy as np


EXPECTED_CID = "ABCDEF019182FAEB416A756E42736C78"
LEVELS = (0.0, 0.25, 0.5, 0.75, 0.8, 1.0)
RATES = (44100, 48000, 96000)
BLOCKS = (17, 64, 511)
PAD_NAMES = (
    "Kick", "Snare", "Rim", "Clap", "Hi Hat - Closed", "Hi Hat - Open",
    "Crash 1", "Ride", "Crash 2", "Tom - Hi", "Tom - Mid", "Tom - Low",
    "Shaker", "Triangle", "FX 1", "FX 2",
)


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def read_pcm24(path: Path) -> tuple[int, np.ndarray]:
    with wave.open(str(path), "rb") as source:
        if source.getsampwidth() != 3:
            raise ValueError(f"{path}: expected 24-bit PCM")
        sample_rate = source.getframerate()
        channels = source.getnchannels()
        raw = np.frombuffer(source.readframes(source.getnframes()), dtype=np.uint8).reshape(-1, 3)
    values = (raw[:, 0].astype(np.int32)
              | (raw[:, 1].astype(np.int32) << 8)
              | (raw[:, 2].astype(np.int32) << 16))
    values = np.where(values & 0x800000, values - 0x1000000, values)
    return sample_rate, values.astype(np.float64).reshape(-1, channels) / 8388608.0


def write_impulse_pcm24(path: Path, sample_rate: int = 48000, frames: int = 2048) -> None:
    values = np.zeros((frames, 2), dtype=np.int32)
    values[0] = (1 << 21, -(1 << 21))
    unsigned = values & 0xFFFFFF
    packed = np.empty((frames, 2, 3), dtype=np.uint8)
    packed[:, :, 0] = unsigned & 0xFF
    packed[:, :, 1] = (unsigned >> 8) & 0xFF
    packed[:, :, 2] = (unsigned >> 16) & 0xFF
    with wave.open(str(path), "wb") as target:
        target.setnchannels(2)
        target.setsampwidth(3)
        target.setframerate(sample_rate)
        target.writeframes(packed.tobytes())


def write_pcm24(path: Path, samples: np.ndarray, sample_rate: int) -> None:
    clipped = np.clip(np.rint(samples * 8388608.0), -8388608, 8388607).astype(np.int32)
    unsigned = clipped & 0xFFFFFF
    packed = np.empty((*clipped.shape, 3), dtype=np.uint8)
    packed[:, :, 0] = unsigned & 0xFF
    packed[:, :, 1] = (unsigned >> 8) & 0xFF
    packed[:, :, 2] = (unsigned >> 16) & 0xFF
    with wave.open(str(path), "wb") as target:
        target.setnchannels(clipped.shape[1])
        target.setsampwidth(3)
        target.setframerate(sample_rate)
        target.writeframes(packed.tobytes())


def metrics(samples: np.ndarray, sample_rate: int) -> dict[str, float]:
    mono = samples.mean(axis=1)
    rms = float(np.sqrt(np.mean(np.square(samples))))
    peak = float(np.max(np.abs(samples)))
    window = np.hanning(len(mono))
    spectrum = np.abs(np.fft.rfft(mono * window))
    frequencies = np.fft.rfftfreq(len(mono), 1.0 / sample_rate)
    total = float(np.sum(spectrum))
    centroid = float(np.sum(frequencies * spectrum) / total) if total else 0.0
    high_energy = float(np.sum(np.square(spectrum[frequencies >= 4000.0])))
    full_energy = float(np.sum(np.square(spectrum)))
    return {
        "peak": peak,
        "rms": rms,
        "peak_dbfs": 20.0 * math.log10(max(peak, 1.0e-15)),
        "rms_dbfs": 20.0 * math.log10(max(rms, 1.0e-15)),
        "spectral_centroid_hz": centroid,
        "high_band_fraction": high_energy / max(full_energy, 1.0e-30),
    }


def delta(effect: dict[str, float], dry: dict[str, float]) -> dict[str, float]:
    return {
        "peak_delta_db": effect["peak_dbfs"] - dry["peak_dbfs"],
        "rms_delta_db": effect["rms_dbfs"] - dry["rms_dbfs"],
        "centroid_ratio": effect["spectral_centroid_hz"] / max(dry["spectral_centroid_hz"], 1.0e-15),
        "high_band_delta_db": 10.0 * math.log10(
            max(effect["high_band_fraction"], 1.0e-30)
            / max(dry["high_band_fraction"], 1.0e-30)
        ),
    }


def align(reference: np.ndarray, candidate: np.ndarray, radius: int = 512) -> dict[str, float | int]:
    length = min(len(reference), len(candidate))
    left = reference[:length].mean(axis=1)
    right = candidate[:length].mean(axis=1)
    if length < 2:
        return {"lag_samples": 0, "correlation": 0.0}
    # This is the same normalized-overlap correlation as the former per-lag
    # loop, but FFT computes all dot products at once.  Reference WAVs are
    # long enough that the old O(radius * frames) implementation dominated
    # actual-host diagnostics.
    fft_length = 1 << (2 * length - 1).bit_length()
    dots = np.fft.irfft(
        np.fft.rfft(right, fft_length) * np.fft.rfft(left[::-1], fft_length),
        fft_length)[:2 * length - 1]
    squared_left = np.concatenate(([0.0], np.cumsum(np.square(left))))
    squared_right = np.concatenate(([0.0], np.cumsum(np.square(right))))
    max_lag = min(radius, length - 1)
    lags = np.arange(-max_lag, max_lag + 1)
    start_left = np.maximum(lags, 0)
    start_right = np.maximum(-lags, 0)
    overlap = length - np.abs(lags)
    left_energy = squared_left[start_left + overlap] - squared_left[start_left]
    right_energy = squared_right[start_right + overlap] - squared_right[start_right]
    dot_products = dots[length - 1 + lags]
    correlations = dot_products / np.maximum(np.sqrt(left_energy * right_energy), 1.0e-30)
    best = int(np.argmax(correlations))
    return {"lag_samples": int(lags[best]), "correlation": float(correlations[best])}


def aligned_nrmse(reference: np.ndarray, candidate: np.ndarray, lag: int) -> float:
    length = min(len(reference), len(candidate))
    reference = reference[:length]
    candidate = candidate[:length]
    if lag < 0:
        reference, candidate = reference[-lag:], candidate[:lag]
    elif lag > 0:
        reference, candidate = reference[:-lag], candidate[lag:]
    return float(np.sqrt(np.mean(np.square(reference - candidate)))
                 / max(np.sqrt(np.mean(np.square(reference))), 1.0e-15))


def aligned_samples(reference: np.ndarray, candidate: np.ndarray, lag: int) -> tuple[np.ndarray, np.ndarray]:
    length = min(len(reference), len(candidate))
    reference, candidate = reference[:length], candidate[:length]
    if lag < 0:
        return reference[-lag:], candidate[:lag]
    if lag > 0:
        return reference[:-lag], candidate[lag:]
    return reference, candidate


def waveform_comparison(reference: np.ndarray, candidate: np.ndarray) -> dict[str, Any]:
    alignment = align(reference, candidate)
    aligned_reference, aligned_candidate = aligned_samples(
        reference, candidate, int(alignment["lag_samples"]))
    return {
        "alignment": alignment,
        "aligned_nrmse": aligned_nrmse(reference, candidate, int(alignment["lag_samples"])),
        "maximum_sample_absolute_error": float(np.max(np.abs(aligned_reference - aligned_candidate))),
        "peak_delta_db": metrics(candidate, 48000)["peak_dbfs"] - metrics(reference, 48000)["peak_dbfs"],
        "rms_delta_db": metrics(candidate, 48000)["rms_dbfs"] - metrics(reference, 48000)["rms_dbfs"],
    }


def onset_and_duration(samples: np.ndarray) -> tuple[int, int]:
    mono = np.abs(samples.mean(axis=1))
    threshold = float(np.max(mono)) * 1.0e-3
    active = np.flatnonzero(mono >= threshold)
    if not len(active):
        return 0, 0
    return int(active[0]), int(active[-1] - active[0] + 1)


def dominant_frequency_hz(samples: np.ndarray, sample_rate: int) -> float:
    mono = samples.mean(axis=1)
    if not np.any(mono):
        return 0.0
    spectrum = np.abs(np.fft.rfft(mono * np.hanning(len(mono))))
    if len(spectrum) < 3:
        return 0.0
    bin_index = int(np.argmax(spectrum[1:]) + 1)
    return float(np.fft.rfftfreq(len(mono), 1.0 / sample_rate)[bin_index])


def resampling_pitch_ratio(source: np.ndarray, effected: np.ndarray) -> float:
    source_mono = source.mean(axis=1)
    effected_mono = effected.mean(axis=1)
    source_positions = np.arange(len(source_mono), dtype=np.float64)

    def score(ratio: float) -> float:
        positions = np.arange(len(effected_mono), dtype=np.float64) * ratio
        positions = positions[positions < len(source_mono) - 1]
        candidate = effected_mono[:len(positions)]
        resampled = np.interp(positions, source_positions, source_mono)
        denominator = float(np.linalg.norm(resampled) * np.linalg.norm(candidate))
        return float(np.dot(resampled, candidate) / denominator) if denominator else -1.0

    ratios = np.linspace(0.9, 1.25, 701)
    scores = np.asarray([score(float(ratio)) for ratio in ratios])
    best = int(np.argmax(scores))
    lower = float(ratios[max(0, best - 2)])
    upper = float(ratios[min(len(ratios) - 1, best + 2)])
    for _ in range(24):
        left = lower + (upper - lower) / 3.0
        right = upper - (upper - lower) / 3.0
        if score(left) < score(right):
            lower = left
        else:
            upper = right
    return (lower + upper) * 0.5


def ambience_measurement(samples: np.ndarray, sample_rate: int) -> dict[str, float]:
    mono = samples.mean(axis=1)
    window_frames = max(1, int(round(sample_rate * 0.050)))
    window_count = len(mono) // window_frames
    if window_count < 2:
        return {"windowed_decay_db": 0.0, "rt60_seconds": 0.0,
                "spectral_centroid_hz": metrics(samples, sample_rate)["spectral_centroid_hz"]}
    windows = mono[:window_count * window_frames].reshape(window_count, window_frames)
    db = 20.0 * np.log10(np.maximum(np.sqrt(np.mean(np.square(windows), axis=1)), 1.0e-15))
    times = np.arange(window_count, dtype=np.float64) * window_frames / sample_rate
    # Compare the same early and 500-ms windows, rather than allowing trailing
    # digital silence to turn an otherwise useful decay measure into -300 dB.
    onset_window = int(np.argmax(db > (float(np.max(db)) - 80.0)))
    late_window = min(window_count - 1, onset_window + 10)
    above_floor = db > (float(np.max(db)) - 80.0)
    fit_times, fit_db = times[above_floor], db[above_floor]
    slope = float(np.polyfit(fit_times, fit_db, 1)[0]) if len(fit_times) > 1 else 0.0
    return {
        "windowed_decay_db": float(db[late_window] - db[onset_window]),
        "rt60_seconds": float(-60.0 / slope) if slope < -1.0e-12 else math.inf,
        "spectral_centroid_hz": metrics(samples, sample_rate)["spectral_centroid_hz"],
    }


def default_pad(index: int) -> dict[str, Any]:
    return {
        "name": PAD_NAMES[index], "factorySampleIds": ["", ""],
        "customSampleName": "", "customSampleSha256": "", "customSampleBase64": "",
        "midiNote": 36 + index, "rootNote": 60, "layerMix": 0.0,
        "gain": math.sqrt(2.0) if index == 0 else 1.0, "pan": 0.0,
        "attackSeconds": 0.0, "attackNormalized": 0.0, "damp": 0.0,
        "pitchSemitones": 0.0, "fineCents": 0.0, "highPass": 0.0,
        "filterEnabled": True, "cutoff": 1.0, "resonance": 0.0,
        "driveEnabled": False, "driveGainDb": 0.0, "distortionMix": 0.0,
        "independentDistortion": False, "distortionDrive": 0.5,
        "compression": 0.0, "crush": 0.0, "reverb": 0.0, "room": 0.0,
        "delay": 0.0,
        "oneShot": True, "muted": False, "solo": False,
        "lpfBypass": False, "hpfBypass": False, "driveBypass": False, "crushBypass": False,
    }


def native_state(input_wav: Path, *, pad: dict[str, Any] | None = None,
                 master: dict[str, Any] | None = None,
                 additional_input_wavs: tuple[Path, ...] = ()) -> dict[str, Any]:
    pads = [default_pad(index) for index in range(16)]
    for index, sample_path in enumerate((input_wav, *additional_input_wavs)):
        sample_bytes = sample_path.read_bytes()
        pads[index].update({
            "gain": math.sqrt(2.0),
            "customSampleName": sample_path.name,
            "customSampleSha256": hashlib.sha256(sample_bytes).hexdigest(),
            "customSampleBase64": base64.b64encode(sample_bytes).decode("ascii"),
        })
        if pad:
            pads[index].update(pad)
    master_state: dict[str, Any] = {
        "filterEnabled": True, "cutoff": 1.0, "resonance": 0.0,
        "delay": 0.0, "distortion": 0.0, "reverb": 0.0, "room": 0.0,
        "crush": 0.0, "compression": 0.0, "gain": 1.0,
    }
    if master:
        master_state.update(master)
    disabled_slice = {"padIndex": 0, "velocity": 0.8, "enabled": False}
    lane = {
        "name": "Host lane", "muted": False, "solo": False,
        "beats": [[dict(disabled_slice)] for _ in range(4)],
    }
    return {
        "schemaVersion": 2, "activePattern": 0,
        "global": {
            "swingPercent": 0.0, "swingReference": 0, "beatsPerMinute": 120.0,
            "defaultVelocity": 0.8, "sequencerEnabled": False, "samplerEnabled": True,
            "auditionDuringPlayback": True, "kitLocked": False, "multiOutput": False,
        },
        "master": master_state,
        "randomizer": {
            "selectedPads": [], "selectedSubdivisions": [],
            "noteSelectionEnabled": True, "slicingEnabled": True,
            "triggeringEnabled": True, "velocityEnabled": True,
            "fillEmptyOnly": False, "triggerChance": 0.5, "spacing": 0.5,
            "minimumVelocity": 0.4, "maximumVelocity": 1.0,
        },
        "ui": {
            "selectedPad": 0, "selectedLane": 0, "selectedBar": 0,
            "patternMode": False, "randomizerVisible": False,
        },
        "pads": pads,
        "patterns": [{
            "name": "Host Pattern", "triggerNote": 12, "quantization": "off",
            "bars": [{"numerator": 4, "denominator": 4}], "lanes": [lane],
        }],
    }


def module_path(bundle: Path) -> Path:
    modules = sorted((bundle / "Contents").rglob("*.so"))
    if len(modules) != 1:
        raise ValueError(f"expected one .so in {bundle}, found {len(modules)}")
    return modules[0]


def render_case(*, host: Path, bundle: Path, plugin_sha256: str, input_wav: Path,
                state_path: Path, output_wav: Path, sample_rate: int, block_size: int,
                active_frames: int, pad: dict[str, Any] | None = None,
                master: dict[str, Any] | None = None,
                additional_input_wavs: tuple[Path, ...] = (),
                midi: tuple[str, ...] = ("0:36:127",)) -> dict[str, Any]:
    state_path.write_text(
        json.dumps(native_state(input_wav, pad=pad, master=master,
                                additional_input_wavs=additional_input_wavs), sort_keys=True,
                   separators=(",", ":")) + "\n", encoding="utf-8")
    state_sha256 = sha256_file(state_path)
    command = [
        str(host), "--plugin", str(bundle), "--state", str(state_path),
        "--output", str(output_wav), "--expect-sha256", plugin_sha256,
        "--expect-cid", EXPECTED_CID, "--sample-rate", str(sample_rate),
        "--block-size", str(block_size), "--active-frames", str(active_frames),
    ]
    for event in midi:
        command.extend(("--midi", event))
    environment = os.environ.copy()
    environment.pop("DISPLAY", None)
    environment.pop("WAYLAND_DISPLAY", None)
    completed = subprocess.run(command, check=False, text=True, env=environment,
                               stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    if completed.returncode != 0:
        raise RuntimeError(
            f"actual VST3 host failed ({completed.returncode}): {' '.join(command)}\n"
            f"stdout:\n{completed.stdout}\nstderr:\n{completed.stderr}")
    host_report = json.loads(completed.stdout)
    if host_report.get("module_sha256") != plugin_sha256:
        raise RuntimeError("host report returned an unexpected module hash")
    return {
        "host": host_report, "state_sha256": state_sha256,
        "input_sha256": sha256_file(input_wav),
        "additional_input_sha256": [sha256_file(path) for path in additional_input_wavs],
        "output_sha256": sha256_file(output_wav),
        "command": command,
    }


REFERENCE_FILES = {
    "dry": "original-dry.wav",
    "pad_neutral": "original-pad-neutral.wav",
    "pad_gain_050": "original-pad-gain-050.wav",
    "pad_pan_075": "original-pad-pan-075.wav",
    "pad_attack_025": "original-pad-attack-025.wav",
    "pad_length_050": "original-pad-length-050.wav",
    "pad_pitch_plus2": "original-pad-pitch-plus2.wav",
    "master_crush_080": "original-master-crush-080.wav",
    "master_compression_080": "original-master-compression-080.wav",
    "master_filter_035": "original-master-filter-035.wav",
    "master_delay_050": "original-master-delay-050.wav",
    "master_reverb_050": "original-master-reverb-050.wav",
    "master_room_050": "original-master-room-050.wav",
}
REFERENCE_CASES: dict[str, dict[str, Any]] = {
    "dry": {"category": "dry"},
    "pad_neutral": {"category": "mapping", "input": "neutral"},
    # The original's normalized 0.5 gain is -12 dB.  The harness's neutral
    # kick is sqrt(2) so its inherited center gain remains compensated.
    "pad_gain_050": {"category": "mapping", "input": "neutral", "pad": {
        "gain": math.sqrt(2.0) * math.pow(10.0, -12.0 / 20.0)}},
    "pad_pan_075": {"category": "mapping", "input": "neutral", "pad": {"pan": 0.5}},
    "pad_attack_025": {"category": "timing-pitch", "input": "neutral",
                       "timing_checks": ("onset", "duration", "correlation"),
                       "pad": {"attackNormalized": 0.25}},
    "pad_length_050": {"category": "timing-pitch", "input": "neutral",
                       "timing_checks": ("onset", "duration", "correlation"),
                       "pad": {"damp": 0.5}},
    "pad_pitch_plus2": {"category": "timing-pitch", "input": "neutral",
                        "timing_checks": ("onset", "duration", "pitch", "correlation"),
                        "pad": {"pitchSemitones": 2.0}},
    "master_crush_080": {"category": "nonlinear", "master": {"crush": 0.8}},
    "master_compression_080": {"category": "nonlinear", "master": {"compression": 0.8}},
    "master_filter_035": {"category": "filter", "master": {"cutoff": 0.35, "resonance": 0.5}},
    "master_delay_050": {"category": "ambience", "input": "neutral",
                         "pad": {"delay": 0.5}, "master": {"delay": 0.5},
                         "informational": True},
    # These captures are preserved and measured, but the original did not
    # audibly change in them.  They cannot manufacture an ambience pass.
    "master_reverb_050": {"category": "ambience", "input": "neutral", "master": {"reverb": 0.5}, "informational": True},
    "master_room_050": {"category": "ambience", "input": "neutral", "master": {"room": 0.5}, "informational": True},
}
LIMITS = {
    "master_compression_080": {
        "peak_delta_db": 0.25, "rms_delta_db": 0.25,
        "centroid_ratio": 0.02, "high_band_delta_db": 0.5,
    },
    "master_crush_080": {
        "peak_delta_db": 0.5, "rms_delta_db": 0.5,
        "centroid_ratio": 0.02, "high_band_delta_db": 1.0,
    },
    "master_filter_035": {
        "peak_delta_db": 1.0, "rms_delta_db": 0.5,
        "centroid_ratio": 0.02, "high_band_delta_db": 12.0,
    },
}
PCM24_LSB = 1.0 / 8388608.0
DRY_ACTUAL_LIMITS = {
    "maximum_absolute_lag_samples": 0,
    "minimum_correlation": 0.999,
    "maximum_aligned_nrmse": 0.001,
    "maximum_peak_absolute_error": PCM24_LSB,
    "maximum_rms_absolute_error": PCM24_LSB,
    "maximum_peak_delta_db": 0.1,
    "maximum_rms_delta_db": 0.1,
}
ACTUAL_ALIGNMENT_LIMITS = {
    "master_compression_080": {
        "maximum_absolute_lag_samples": 1,
        "minimum_correlation": 0.99,
        "maximum_aligned_nrmse": 0.13,
    },
    "master_crush_080": {
        "maximum_absolute_lag_samples": 1,
        "minimum_correlation": 0.90,
        "maximum_aligned_nrmse": 0.60,
    },
    "master_filter_035": {
        "maximum_absolute_lag_samples": 8,
        "minimum_correlation": 0.95,
        "maximum_aligned_nrmse": 0.32,
    },
}
REQUIRED_REFERENCE_CATEGORIES = (
    "dry", "mapping", "timing-pitch", "filter", "nonlinear", "ambience",
)
AVAILABLE_REFERENCE_CATEGORIES = REQUIRED_REFERENCE_CATEGORIES
MAPPING_LIMITS = {
    "maximum_absolute_lag_samples": 0,
    "maximum_sample_absolute_error": PCM24_LSB,
    "maximum_peak_delta_db": 0.1,
    "maximum_rms_delta_db": 0.1,
}
# The pan reference is a transfer measurement: the licensed neutral render is
# first quantized to PCM24, decoded as the reconstruction's private input,
# panned, then quantized to PCM24 again.  That extra quantization can add one
# additional LSB.  All direct mapping cases retain the one-LSB boundary.
MAPPING_SAMPLE_ERROR_LIMITS = {
    "pad_pan_075": 2.0 * PCM24_LSB,
}
TIMING_PITCH_LIMITS = {
    "maximum_onset_error_samples": 1,
    "maximum_duration_error_samples": 1,
    "maximum_pitch_error_cents": 1.0,
    "minimum_correlation": 0.995,
}
AMBIENCE_LIMITS = {
    "maximum_windowed_decay_error_db": 1.5,
    "maximum_rt60_relative_error": 0.10,
    "maximum_centroid_relative_error": 0.10,
}


def evaluate_reference_acceptance(
        dry_comparison: dict[str, Any], comparisons: dict[str, Any], *,
        suite_mode: str) -> dict[str, Any]:
    if suite_mode not in ("full", "diagnostic"):
        raise ValueError(f"unsupported reference suite mode: {suite_mode}")

    feature_checks = {
        name: {key: comparisons[name]["absolute_error"][key] <= maximum
               for key, maximum in effect_limits.items()}
        for name, effect_limits in LIMITS.items()
    }
    dry_alignment = dry_comparison["alignment"]
    dry_checks = {
        "lag_samples": abs(int(dry_alignment["lag_samples"]))
        <= DRY_ACTUAL_LIMITS["maximum_absolute_lag_samples"],
        "correlation": float(dry_alignment["correlation"])
        >= DRY_ACTUAL_LIMITS["minimum_correlation"],
        "aligned_nrmse": float(dry_comparison["aligned_nrmse"])
        <= DRY_ACTUAL_LIMITS["maximum_aligned_nrmse"],
        "peak_absolute_error": float(dry_comparison["peak_absolute_error"])
        <= DRY_ACTUAL_LIMITS["maximum_peak_absolute_error"],
        "rms_absolute_error": float(dry_comparison["rms_absolute_error"])
        <= DRY_ACTUAL_LIMITS["maximum_rms_absolute_error"],
        "peak_delta_db": abs(float(dry_comparison["peak_delta_db"]))
        <= DRY_ACTUAL_LIMITS["maximum_peak_delta_db"],
        "rms_delta_db": abs(float(dry_comparison["rms_delta_db"]))
        <= DRY_ACTUAL_LIMITS["maximum_rms_delta_db"],
    }
    actual_alignment_checks: dict[str, dict[str, bool]] = {}
    for name, limits in ACTUAL_ALIGNMENT_LIMITS.items():
        comparison = comparisons[name]
        alignment = comparison["alignment"]
        actual_alignment_checks[name] = {
            "lag_samples": abs(int(alignment["lag_samples"]))
            <= limits["maximum_absolute_lag_samples"],
            "correlation": float(alignment["correlation"])
            >= limits["minimum_correlation"],
            "aligned_nrmse": float(comparison["aligned_nrmse"])
            <= limits["maximum_aligned_nrmse"],
        }

    mapping_checks: dict[str, dict[str, bool]] = {}
    timing_pitch_checks: dict[str, dict[str, bool]] = {}
    ambience_checks: dict[str, dict[str, bool]] = {
        "actual_vst3_functional": (
            comparisons["_ambience_functional"]["checks"]
            if "_ambience_functional" in comparisons
            else {"measurement_present": False}
        )
    }
    for name, comparison in comparisons.items():
        if name == "_ambience_functional":
            continue
        case = REFERENCE_CASES[name]
        if case["category"] == "mapping":
            alignment = comparison["alignment"]
            sample_error_limit = MAPPING_SAMPLE_ERROR_LIMITS.get(
                name, MAPPING_LIMITS["maximum_sample_absolute_error"])
            mapping_checks[name] = {
                "lag_samples": abs(int(alignment["lag_samples"]))
                <= MAPPING_LIMITS["maximum_absolute_lag_samples"],
                "sample_absolute_error": float(comparison["maximum_sample_absolute_error"])
                <= sample_error_limit,
                "peak_delta_db": abs(float(comparison["peak_delta_db"]))
                <= MAPPING_LIMITS["maximum_peak_delta_db"],
                "rms_delta_db": abs(float(comparison["rms_delta_db"]))
                <= MAPPING_LIMITS["maximum_rms_delta_db"],
            }
        elif case["category"] == "timing-pitch":
            timing = comparison["timing_pitch"]
            required = set(case["timing_checks"])
            checks = {
                "onset_samples": abs(int(timing["onset_error_samples"]))
                <= TIMING_PITCH_LIMITS["maximum_onset_error_samples"],
                "duration_samples": abs(int(timing["duration_error_samples"]))
                <= TIMING_PITCH_LIMITS["maximum_duration_error_samples"],
                "pitch_cents": float(timing["pitch_error_cents"])
                <= TIMING_PITCH_LIMITS["maximum_pitch_error_cents"],
                "correlation": float(comparison["alignment"]["correlation"])
                >= TIMING_PITCH_LIMITS["minimum_correlation"],
            }
            timing_pitch_checks[name] = {
                key: value for key, value in checks.items()
                if {"onset_samples": "onset", "duration_samples": "duration",
                    "pitch_cents": "pitch", "correlation": "correlation"}[key]
                in required
            }
        elif case["category"] == "ambience" and not case.get("informational", False):
            ambience = comparison["ambience"]
            ambience_checks[name] = {
                "windowed_decay_db": float(ambience["windowed_decay_error_db"])
                <= AMBIENCE_LIMITS["maximum_windowed_decay_error_db"],
                "rt60_relative_error": float(ambience["rt60_relative_error"])
                <= AMBIENCE_LIMITS["maximum_rt60_relative_error"],
                "centroid_relative_error": float(ambience["centroid_relative_error"])
                <= AMBIENCE_LIMITS["maximum_centroid_relative_error"],
            }

    missing_categories = [
        category for category in REQUIRED_REFERENCE_CATEGORIES
        if category not in AVAILABLE_REFERENCE_CATEGORIES
    ]
    measured_pass = (
        all(feature for effect in feature_checks.values() for feature in effect.values())
        and all(dry_checks.values())
        and all(check for effect in actual_alignment_checks.values()
                for check in effect.values())
        and all(check for effect in mapping_checks.values() for check in effect.values())
        and all(check for effect in timing_pitch_checks.values() for check in effect.values())
        and all(check for effect in ambience_checks.values() for check in effect.values())
    )
    suite_status = "partial" if missing_categories else "full"
    if not measured_pass:
        result = "fail"
    elif suite_status == "full":
        result = "pass"
    else:
        result = "partial" if suite_mode == "diagnostic" else "fail"
    return {
        "result": result,
        "suite_status": suite_status,
        "suite_mode": suite_mode,
        "required_categories": list(REQUIRED_REFERENCE_CATEGORIES),
        "available_categories": list(AVAILABLE_REFERENCE_CATEGORIES),
        "missing_categories": missing_categories,
        "limits": {
            "feature_deltas": LIMITS,
            "dry_actual": DRY_ACTUAL_LIMITS,
            "actual_alignment": ACTUAL_ALIGNMENT_LIMITS,
            "mapping": MAPPING_LIMITS,
            "mapping_sample_error_overrides": MAPPING_SAMPLE_ERROR_LIMITS,
            "timing_pitch": TIMING_PITCH_LIMITS,
            "ambience": AMBIENCE_LIMITS,
        },
        "checks": {
            "feature_deltas": feature_checks,
            "dry_actual": dry_checks,
            "actual_alignment": actual_alignment_checks,
            "mapping": mapping_checks,
            "timing_pitch": timing_pitch_checks,
            "ambience": ambience_checks,
        },
    }


def run_references(validation_root: Path, artifacts: Path, *, host: Path,
                   bundle: Path, plugin_sha256: str, block_size: int,
                   suite_mode: str = "full") -> dict[str, Any]:
    original_audio: dict[str, np.ndarray] = {}
    original_metrics: dict[str, dict[str, float]] = {}
    original_hashes: dict[str, str] = {}
    sample_rate = 0
    for name, filename in REFERENCE_FILES.items():
        path = validation_root / filename
        current_rate, samples = read_pcm24(path)
        if sample_rate and current_rate != sample_rate:
            raise ValueError("reference renders use different sample rates")
        sample_rate = current_rate
        original_audio[name] = samples
        original_metrics[name] = metrics(samples, sample_rate)
        original_hashes[name] = sha256_file(path)

    ambience_neutral_path = validation_root / "original-pad-neutral-4s.wav"
    ambience_rate, ambience_neutral = read_pcm24(ambience_neutral_path)
    if ambience_rate != sample_rate:
        raise ValueError("ambience neutral render uses a different sample rate")
    original_hashes["pad_neutral_4s"] = sha256_file(ambience_neutral_path)

    reconstructed_audio: dict[str, np.ndarray] = {}
    reconstructed_metrics: dict[str, dict[str, float]] = {}
    renders: dict[str, dict[str, Any]] = {}
    base_frames = len(original_audio["pad_neutral"])
    loop_frames = base_frames // 2
    base_sources: list[Path] = []
    for index in range(2):
        path = artifacts / f"isolated-pad-neutral-{index}.wav"
        write_pcm24(path, original_audio["pad_neutral"][
            index * loop_frames:(index + 1) * loop_frames], sample_rate)
        base_sources.append(path)
    ambience_sources: list[Path] = []
    for index in range(len(ambience_neutral) // loop_frames):
        path = artifacts / f"isolated-pad-ambience-{index}.wav"
        write_pcm24(path, ambience_neutral[
            index * loop_frames:(index + 1) * loop_frames], sample_rate)
        ambience_sources.append(path)
    for name, case in REFERENCE_CASES.items():
        uses_neutral = case.get("input") == "neutral"
        target_frames = len(original_audio[name])
        sources = (ambience_sources
                   if uses_neutral and target_frames > base_frames else base_sources)
        input_wav = sources[0] if uses_neutral else validation_root / REFERENCE_FILES["dry"]
        output = artifacts / f"actual-{name}.wav"
        renders[name] = render_case(
            host=host, bundle=bundle, plugin_sha256=plugin_sha256,
            input_wav=input_wav, state_path=artifacts / f"actual-{name}.bslpreset",
            output_wav=output, sample_rate=sample_rate, block_size=block_size,
            active_frames=target_frames, pad=case.get("pad"), master=case.get("master"),
            additional_input_wavs=(tuple(sources[1:]) if uses_neutral else ()),
            midi=(tuple(f"{index * loop_frames}:{36 + index}:127"
                        for index in range(len(sources)))
                  if uses_neutral else ("0:36:127",)))
        rendered_rate, rendered = read_pcm24(output)
        if rendered_rate != sample_rate or len(rendered) < target_frames:
            raise RuntimeError(f"{name}: actual host WAV has wrong rate or duration")
        reconstructed_audio[name] = rendered[:target_frames]
        reconstructed_metrics[name] = metrics(rendered[:target_frames], sample_rate)

    ambience_frames = len(ambience_neutral)
    ambience_neutral_output = artifacts / "actual-ambience-functional-neutral.wav"
    renders["ambience_functional_neutral"] = render_case(
        host=host, bundle=bundle, plugin_sha256=plugin_sha256,
        input_wav=ambience_sources[0],
        state_path=artifacts / "actual-ambience-functional-neutral.bslpreset",
        output_wav=ambience_neutral_output, sample_rate=sample_rate,
        block_size=block_size, active_frames=ambience_frames,
        additional_input_wavs=tuple(ambience_sources[1:]),
        midi=tuple(f"{index * loop_frames}:{36 + index}:127"
                   for index in range(len(ambience_sources))))
    ambience_render_rate, ambience_render = read_pcm24(ambience_neutral_output)
    if ambience_render_rate != sample_rate or len(ambience_render) < ambience_frames:
        raise RuntimeError("functional ambience neutral WAV has wrong rate or duration")
    ambience_render = ambience_render[:ambience_frames]

    comparisons: dict[str, Any] = {}
    for name in REFERENCE_FILES:
        if name == "dry":
            continue
        original_delta = delta(original_metrics[name], original_metrics["dry"])
        reconstructed_delta = delta(reconstructed_metrics[name], reconstructed_metrics["dry"])
        timing_case = REFERENCE_CASES[name]["category"] == "timing-pitch"
        comparison_original = (original_audio[name][loop_frames:]
                               if timing_case else original_audio[name])
        comparison_reconstructed = (reconstructed_audio[name][loop_frames:]
                                    if timing_case else reconstructed_audio[name])
        comparison = {
            "original_delta": original_delta, "reconstruction_delta": reconstructed_delta,
            "absolute_error": {key: abs(original_delta[key] - reconstructed_delta[key])
                               for key in original_delta},
            **waveform_comparison(comparison_original, comparison_reconstructed),
        }
        if timing_case:
            original_onset, original_duration = onset_and_duration(comparison_original)
            reconstructed_onset, reconstructed_duration = onset_and_duration(
                comparison_reconstructed)
            pitch_error_cents = 0.0
            original_frequency = dominant_frequency_hz(comparison_original, sample_rate)
            reconstructed_frequency = dominant_frequency_hz(
                comparison_reconstructed, sample_rate)
            if "pitch" in REFERENCE_CASES[name]["timing_checks"]:
                pitch_source = original_audio["pad_neutral"][loop_frames:]
                original_ratio = resampling_pitch_ratio(pitch_source, comparison_original)
                reconstructed_ratio = resampling_pitch_ratio(
                    pitch_source, comparison_reconstructed)
                pitch_error_cents = abs(1200.0 * math.log2(
                    reconstructed_ratio / original_ratio))
            comparison["timing_pitch"] = {
                "original_onset_samples": original_onset,
                "reconstruction_onset_samples": reconstructed_onset,
                "onset_error_samples": reconstructed_onset - original_onset,
                "original_duration_samples": original_duration,
                "reconstruction_duration_samples": reconstructed_duration,
                "duration_error_samples": reconstructed_duration - original_duration,
                "original_pitch_hz": original_frequency,
                "reconstruction_pitch_hz": reconstructed_frequency,
                "pitch_error_cents": pitch_error_cents,
            }
        if REFERENCE_CASES[name]["category"] == "ambience":
            original_ambience = ambience_measurement(original_audio[name], sample_rate)
            reconstructed_ambience = ambience_measurement(reconstructed_audio[name], sample_rate)
            original_rt60 = original_ambience["rt60_seconds"]
            reconstructed_rt60 = reconstructed_ambience["rt60_seconds"]
            if math.isinf(original_rt60) and math.isinf(reconstructed_rt60):
                rt60_relative_error = 0.0
            else:
                rt60_relative_error = abs(reconstructed_rt60 - original_rt60) / max(
                    abs(original_rt60), 1.0e-15)
            comparison["ambience"] = {
                "original": original_ambience,
                "reconstruction": reconstructed_ambience,
                "windowed_decay_error_db": abs(
                    reconstructed_ambience["windowed_decay_db"]
                    - original_ambience["windowed_decay_db"]),
                "rt60_relative_error": rt60_relative_error,
                "centroid_relative_error": abs(
                    reconstructed_ambience["spectral_centroid_hz"]
                    - original_ambience["spectral_centroid_hz"])
                / max(abs(original_ambience["spectral_centroid_hz"]), 1.0e-15),
                "authoritative": not REFERENCE_CASES[name].get("informational", False),
            }
        comparisons[name] = comparison

    ambience_residuals = {
        name: reconstructed_audio[f"master_{name}_050"] - ambience_render
        for name in ("delay", "reverb", "room")
    }
    def tap_energy(samples: np.ndarray, offset: int) -> float:
        values: list[float] = []
        half_window = 128
        for hit in range(len(ambience_sources)):
            centre = hit * loop_frames + offset
            if centre + half_window > len(samples):
                continue
            window = samples[centre - half_window:centre + half_window]
            values.append(float(np.sqrt(np.mean(np.square(window)))))
        return float(np.mean(values)) if values else 0.0

    tap_frames = {
        "room": int(sample_rate * 0.065),
        "delay": int(sample_rate * 0.25),
        "reverb": int(sample_rate * 0.37),
    }
    tap_energies = {
        effect: {tap: tap_energy(residual, offset) for tap, offset in tap_frames.items()}
        for effect, residual in ambience_residuals.items()
    }
    residual_rms = {
        effect: float(np.sqrt(np.mean(np.square(residual))))
        for effect, residual in ambience_residuals.items()
    }
    comparisons["_ambience_functional"] = {
        "authority": "manual-semantics-actual-vst3",
        "residual_rms": residual_rms,
        "tap_energy": tap_energies,
        "checks": {
            "delay_changes_output": residual_rms["delay"] > 1.0e-5,
            "reverb_changes_output": residual_rms["reverb"] > 1.0e-5,
            "room_changes_output": residual_rms["room"] > 1.0e-5,
            "delay_quarter_second_return": tap_energies["delay"]["delay"] > 1.0e-4,
            "reverb_is_longer_than_room": (
                tap_energies["reverb"]["reverb"]
                > tap_energies["room"]["reverb"] * 2.0),
            "room_is_earlier_than_reverb": (
                tap_energies["room"]["room"]
                > tap_energies["reverb"]["room"] * 2.0),
        },
    }
    dry_alignment = align(original_audio["dry"], reconstructed_audio["dry"])
    dry_comparison = {
        "alignment": dry_alignment,
        "aligned_nrmse": aligned_nrmse(
            original_audio["dry"], reconstructed_audio["dry"],
            int(dry_alignment["lag_samples"])),
        "peak_absolute_error": abs(
            original_metrics["dry"]["peak"] - reconstructed_metrics["dry"]["peak"]),
        "rms_absolute_error": abs(
            original_metrics["dry"]["rms"] - reconstructed_metrics["dry"]["rms"]),
        "peak_delta_db": (
            reconstructed_metrics["dry"]["peak_dbfs"]
            - original_metrics["dry"]["peak_dbfs"]),
        "rms_delta_db": (
            reconstructed_metrics["dry"]["rms_dbfs"]
            - original_metrics["dry"]["rms_dbfs"]),
    }
    acceptance = evaluate_reference_acceptance(
        dry_comparison, comparisons, suite_mode=suite_mode)
    return {
        "authority": "licensed-original-reference",
        "method": (
            "The licensed original dry WAV, or one isolated period of the licensed neutral pad "
            "render with its two-trigger schedule restored, is embedded as a private native-v2 "
            "custom sample; "
            "every reconstruction WAV is rendered by the exact built VST3 through "
            "actual_vst3_host. Python computes measurements only."),
        "sample_rate": sample_rate, "block_size": block_size,
        "active_frames": {name: len(audio) for name, audio in original_audio.items()},
        "input_alignment": dry_alignment,
        "dry_comparison": dry_comparison,
        "original_hashes": original_hashes, "original_metrics": original_metrics,
        "reconstruction_metrics": reconstructed_metrics, "renders": renders,
        "comparisons": comparisons,
        "informational_references": {
            name: "The licensed capture did not change audibly; retained as a diagnostic only."
            for name, case in REFERENCE_CASES.items() if case.get("informational", False)
        },
        "acceptance": acceptance,
    }


def bool_level(value: float) -> bool:
    return value >= 0.5


Control = tuple[str, str, Callable[[float], Any]]
COVERAGE_CONTROLS: tuple[Control, ...] = (
    ("pad", "layerMix", lambda x: x), ("pad", "gain", lambda x: x),
    ("pad", "pan", lambda x: -1.0 + 2.0 * x),
    ("pad", "attackSeconds", lambda x: x),
    ("pad", "attackNormalized", lambda x: x), ("pad", "damp", lambda x: x),
    ("pad", "pitchSemitones", lambda x: -12.0 + 24.0 * x),
    ("pad", "fineCents", lambda x: -100.0 + 200.0 * x),
    ("pad", "highPass", lambda x: x), ("pad", "filterEnabled", bool_level),
    ("pad", "cutoff", lambda x: x), ("pad", "resonance", lambda x: x),
    ("pad", "driveEnabled", bool_level),
    ("pad", "driveGainDb", lambda x: -60.0 + 120.0 * x),
    ("pad", "distortionMix", lambda x: x),
    ("pad", "compression", lambda x: x), ("pad", "crush", lambda x: x),
    ("pad", "reverb", lambda x: x), ("pad", "room", lambda x: x),
    ("pad", "delay", lambda x: x),
    ("pad", "oneShot", bool_level), ("pad", "muted", bool_level),
    ("pad", "solo", bool_level), ("master", "filterEnabled", bool_level),
    ("master", "cutoff", lambda x: x), ("master", "resonance", lambda x: x),
    ("master", "delay", lambda x: x), ("master", "distortion", lambda x: x),
    ("master", "reverb", lambda x: x), ("master", "room", lambda x: x),
    ("master", "crush", lambda x: x), ("master", "compression", lambda x: x),
    ("master", "gain", lambda x: x),
)


def intended_coverage_cases() -> int:
    return len(COVERAGE_CONTROLS) * len(LEVELS) * len(RATES) * len(BLOCKS)


def run_coverage(input_wav: Path, artifacts: Path, *, host: Path, bundle: Path,
                 plugin_sha256: str, limit: int | None = None) -> dict[str, Any]:
    input_rate, input_audio = read_pcm24(input_wav)
    cases: list[dict[str, Any]] = []
    intended = intended_coverage_cases()
    for scope, control, convert in COVERAGE_CONTROLS:
        for normalized in LEVELS:
            for sample_rate in RATES:
                for block_size in BLOCKS:
                    if limit is not None and len(cases) >= limit:
                        return {"authority": "coverage-only-no-licensed-original-reference",
                                "input_sha256": sha256_file(input_wav),
                                "input_sample_rate": input_rate,
                                "input_frames": len(input_audio),
                                "complete": False, "intended_cases": intended,
                                "executed_cases": len(cases), "cases": cases}
                    value = convert(normalized)
                    active_frames = int(math.ceil(len(input_audio) * sample_rate / input_rate))
                    pad = {control: value} if scope == "pad" else None
                    master = {control: value} if scope == "master" else None
                    if scope == "pad" and control == "filterEnabled":
                        pad.update({"cutoff": 0.35, "resonance": 0.5})
                    elif scope == "pad" and control == "resonance":
                        pad["cutoff"] = 0.35
                    elif scope == "pad" and control == "driveEnabled":
                        pad.update({"driveGainDb": 12.0, "distortionMix": 1.0})
                    elif scope == "pad" and control == "driveGainDb":
                        pad.update({"driveEnabled": True, "distortionMix": 1.0})
                    elif scope == "pad" and control == "distortionMix":
                        pad.update({"driveEnabled": True, "driveGainDb": 12.0})
                    elif scope == "master" and control == "filterEnabled":
                        master.update({"cutoff": 0.35, "resonance": 0.5})
                    elif scope == "master" and control == "resonance":
                        master["cutoff"] = 0.35
                    midi = (("0:36:127", f"{max(1, active_frames // 4)}:36:0")
                            if scope == "pad" and control == "oneShot"
                            else ("0:36:127",))
                    rendered = render_case(
                        host=host, bundle=bundle, plugin_sha256=plugin_sha256,
                        input_wav=input_wav, state_path=artifacts / "current-coverage.bslpreset",
                        output_wav=artifacts / "current-coverage.wav",
                        sample_rate=sample_rate, block_size=block_size,
                        active_frames=active_frames, pad=pad, master=master, midi=midi)
                    rate, audio = read_pcm24(artifacts / "current-coverage.wav")
                    cases.append({
                        "scope": scope, "control": control,
                        "normalized_setting": normalized, "state_value": value,
                        "effective_pad_overrides": pad,
                        "effective_master_overrides": master,
                        "midi_events": list(midi),
                        "sample_rate": sample_rate, "block_size": block_size,
                        "active_frames": active_frames,
                        "output_sha256": rendered["output_sha256"],
                        "state_sha256": rendered["state_sha256"],
                        "output_metrics": metrics(audio[:active_frames], rate),
                        "tail_frames": rendered["host"]["tail_frames"],
                    })
    return {"authority": "coverage-only-no-licensed-original-reference",
            "input_sha256": sha256_file(input_wav),
            "input_sample_rate": input_rate, "input_frames": len(input_audio),
            "complete": True, "intended_cases": intended,
            "executed_cases": len(cases), "cases": cases}


def main() -> int:
    root = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser()
    parser.add_argument("validation_root", type=Path, nargs="?")
    parser.add_argument("--plugin", type=Path, default=root /
                        "build/release/BeatScholarLinux_artefacts/Release/VST3/Beat Scholar Linux.vst3")
    parser.add_argument("--host", type=Path,
                        default=root / "build/release/tools/actual_vst3_host")
    parser.add_argument("--artifacts-dir", type=Path)
    parser.add_argument("--output", type=Path)
    parser.add_argument("--mode", choices=("all", "references", "coverage", "smoke"),
                        default="all")
    parser.add_argument("--reference-block-size", type=int, default=64)
    parser.add_argument(
        "--reference-suite-mode", choices=("full", "diagnostic"), default="full",
        help=("full enforces the required category suite; diagnostic runs the four "
              "available references without allowing a full acceptance pass"))
    parser.add_argument("--coverage-limit", type=int)
    args = parser.parse_args()

    if not args.plugin.is_dir():
        parser.error(f"plugin bundle does not exist: {args.plugin}")
    if not args.host.is_file():
        parser.error(f"actual VST3 host does not exist: {args.host}")
    if args.mode != "smoke" and args.validation_root is None:
        parser.error("validation_root is required outside smoke mode")

    module = module_path(args.plugin)
    plugin_sha256 = sha256_file(module)
    default_artifacts = ((args.validation_root or root / "build/release/validation")
                         / "actual-vst3-parity")
    artifacts = args.artifacts_dir or default_artifacts
    artifacts.mkdir(parents=True, exist_ok=True)
    report: dict[str, Any] = {
        "schema_version": 2, "renderer": "actual-vst3-host",
        "plugin_bundle": str(args.plugin.resolve()), "plugin_module": str(module.resolve()),
        "plugin_sha256": plugin_sha256, "expected_cid": EXPECTED_CID,
    }

    accepted = True
    if args.mode == "smoke":
        with tempfile.TemporaryDirectory(dir=artifacts) as temporary:
            scratch = Path(temporary)
            impulse = scratch / "impulse.wav"
            write_impulse_pcm24(impulse)
            smoke = render_case(
                host=args.host, bundle=args.plugin, plugin_sha256=plugin_sha256,
                input_wav=impulse, state_path=scratch / "smoke.bslpreset",
                output_wav=artifacts / "actual-vst3-smoke.wav", sample_rate=48000,
                block_size=64, active_frames=2048)
            _, audio = read_pcm24(artifacts / "actual-vst3-smoke.wav")
            peak = float(np.max(np.abs(audio[:2048])))
            if peak <= 0.0:
                raise RuntimeError("actual VST3 smoke render is silent")
            report["smoke"] = {**smoke, "active_peak": peak}
    else:
        assert args.validation_root is not None
        if args.mode in ("all", "references"):
            references = run_references(
                args.validation_root, artifacts, host=args.host, bundle=args.plugin,
                plugin_sha256=plugin_sha256, block_size=args.reference_block_size,
                suite_mode=args.reference_suite_mode)
            report["references"] = references
            accepted = references["acceptance"]["result"] == "pass"
        if args.mode in ("all", "coverage"):
            coverage = run_coverage(
                args.validation_root / REFERENCE_FILES["dry"], artifacts,
                host=args.host, bundle=args.plugin, plugin_sha256=plugin_sha256,
                limit=args.coverage_limit)
            report["coverage"] = coverage
            if args.coverage_limit is None:
                accepted = accepted and bool(coverage["complete"])

    if args.mode == "coverage":
        report["result"] = "coverage-only"
    else:
        report["result"] = "pass" if accepted else "fail"
    if args.reference_suite_mode == "diagnostic" and args.mode in ("all", "references"):
        report["diagnostic_only"] = True
    rendered = json.dumps(report, indent=2, sort_keys=True) + "\n"
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(rendered, encoding="utf-8")
    print(rendered, end="")
    if args.mode == "coverage" or (
            args.reference_suite_mode == "diagnostic"
            and args.mode in ("all", "references")):
        return 0
    return 0 if accepted else 1


if __name__ == "__main__":
    raise SystemExit(main())
