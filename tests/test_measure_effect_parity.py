from __future__ import annotations

import importlib.util
import copy
import tempfile
import unittest
from unittest.mock import patch
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location(
    "measure_effect_parity", ROOT / "tools/measure_effect_parity.py")
assert SPEC is not None and SPEC.loader is not None
PARITY = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(PARITY)


class EffectParityHarnessTests(unittest.TestCase):
    def test_reference_settling_preserves_crush_capture_phase(self) -> None:
        case = PARITY.REFERENCE_CASES["master_crush_080"]
        for rate in (44100, 48000, 96000):
            frames = PARITY.reference_pre_roll_frames(rate, case)
            self.assertGreaterEqual(frames, rate * 0.1)
            self.assertLess(frames, rate * 0.1 + 17)
            self.assertEqual(frames % 17, 0)
        self.assertEqual(PARITY.reference_pre_roll_frames(48000, {}), 4800)
        self.assertEqual(PARITY.reference_pre_roll_frames(48000, case), 4811)

    def test_render_pre_roll_is_explicit_and_receipted(self) -> None:
        import subprocess
        with tempfile.TemporaryDirectory() as directory:
            work = Path(directory)
            sample, output = work / "input.wav", work / "output.wav"
            PARITY.write_impulse_pcm24(sample, frames=128)
            output.write_bytes(sample.read_bytes())
            for frames in (0, 4800):
                receipt = PARITY.json.dumps({"module_sha256": "fixture",
                                            "pre_roll_frames": frames})
                with patch.object(PARITY.subprocess, "run", return_value=
                        subprocess.CompletedProcess([], 0, receipt, "")) as run:
                    report = PARITY.render_case(host=work / "host", bundle=work / "plugin",
                        plugin_sha256="fixture", input_wav=sample,
                        state_path=work / "state.json", output_wav=output,
                        sample_rate=48000, block_size=64, active_frames=128,
                        pre_roll_frames=frames)
                command = run.call_args.args[0]
                self.assertEqual("--pre-roll-frames" in command, frames != 0)
                self.assertEqual(report["host"]["pre_roll_frames"], frames)
                self.assertEqual(command[command.index("--midi") + 1], "0:36:127")
            with patch.object(PARITY.subprocess, "run", return_value=
                    subprocess.CompletedProcess([], 0,
                        PARITY.json.dumps({"module_sha256": "fixture"}), "")):
                with self.assertRaisesRegex(RuntimeError, "pre-roll duration"):
                    PARITY.render_case(host=work / "host", bundle=work / "plugin",
                        plugin_sha256="fixture", input_wav=sample,
                        state_path=work / "state.json", output_wav=output,
                        sample_rate=48000, block_size=64, active_frames=128,
                        pre_roll_frames=4800)

    def test_reference_replay_restores_trim_without_clipping(self) -> None:
        samples = PARITY.np.array([[0.1, -0.2], [0.8, -0.9], [0.0, 0.0]])
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "replay.wav"
            PARITY.write_reference_replay_wav(path, samples, 48000)
            raw = path.read_bytes()
        self.assertEqual(raw[:4], b"RIFF")
        self.assertEqual(raw[8:16], b"WAVEfmt ")
        self.assertEqual(PARITY.struct.unpack("<HHIIHH", raw[20:36]),
                         (3, 2, 48000, 384000, 8, 32))
        self.assertEqual(raw[36:40], b"fact")
        self.assertEqual(PARITY.struct.unpack("<II", raw[40:48]), (4, 3))
        self.assertEqual(raw[48:52], b"data")
        decoded = PARITY.np.frombuffer(raw[56:], dtype="<f4").reshape(-1, 2)
        self.assertGreater(float(PARITY.np.max(PARITY.np.abs(decoded))), 1.0)
        restored = decoded * PARITY.NATIVE_PAD_OUTPUT_TRIM
        self.assertLess(float(PARITY.np.max(PARITY.np.abs(restored - samples))),
                        PARITY.PCM24_LSB)

    def test_reference_replay_rejects_nonfinite_samples_and_nonlinear_pads(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "replay.wav"
            for value in (float("nan"), float("inf"), -float("inf")):
                with self.assertRaisesRegex(ValueError, "finite samples"):
                    PARITY.write_reference_replay_wav(
                        path, PARITY.np.full((2, 2), value), 48000)
                self.assertFalse(path.exists())
        for settings in ({"driveEnabled": True}, {"compression": 0.25},
                         {"crush": 0.8}, {"distortionMix": 0.5}):
            with self.assertRaisesRegex(ValueError, "linear pad"):
                PARITY.validate_replay_pad(settings)
        for case in PARITY.REFERENCE_CASES.values():
            PARITY.validate_replay_pad(case.get("pad"))

    def test_native_v2_state_embeds_private_input_by_hash(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            sample = Path(directory) / "input.wav"
            PARITY.write_impulse_pcm24(sample, frames=128)
            state = PARITY.native_state(
                sample, pad={"compression": 0.25}, master={"crush": 0.8})
            expected_hash = PARITY.hashlib.sha256(sample.read_bytes()).hexdigest()

        self.assertEqual(state["schemaVersion"], 2)
        self.assertEqual(len(state["pads"]), 16)
        self.assertEqual(state["pads"][0]["customSampleSha256"], expected_hash)
        self.assertTrue(state["pads"][0]["customSampleBase64"])
        self.assertEqual(state["pads"][0]["compression"], 0.25)
        self.assertEqual(state["master"]["crush"], 0.8)
        self.assertEqual(state["master"]["distortionReturn"], 1.0)
        self.assertFalse(state["global"]["sequencerEnabled"])
        self.assertIs(state["global"]["auxEnabled"], True)
        self.assertIs(state["global"]["masterEnabled"], True)
        self.assertIs(state["master"]["filterEnabled"], False)
        for pad in state["pads"]:
            self.assertEqual(pad["auxGain"], 0.0)
            self.assertIs(pad["filterEnabled"], False)
            self.assertIs(pad["hpfBypass"], True)
            for effect in ("lpfBypass", "driveBypass", "crushBypass"):
                self.assertIs(pad[effect], False)

    def test_coverage_matrix_has_every_routed_control_and_grid_point(self) -> None:
        controls = {(scope, name) for scope, name, _ in PARITY.COVERAGE_CONTROLS}
        expected_pad = {
            "layerMix", "gain", "pan", "attackSeconds", "attackNormalized", "damp",
            "pitchSemitones", "fineCents", "highPass", "filterEnabled",
            "cutoff", "resonance", "driveEnabled", "driveGainDb",
            "distortionMix", "compression", "crush", "reverb", "room", "delay", "auxGain",
            "oneShot", "muted", "solo",
        }
        expected_master = {
            "filterEnabled", "cutoff", "resonance", "delay", "distortion", "distortionReturn",
            "reverb", "room", "crush", "compression", "gain",
        }
        self.assertEqual({name for scope, name in controls if scope == "pad"}, expected_pad)
        self.assertEqual({name for scope, name in controls if scope == "master"}, expected_master)
        self.assertEqual(PARITY.LEVELS, (0.0, 0.25, 0.5, 0.75, 0.8, 1.0))
        self.assertEqual(PARITY.RATES, (44100, 48000, 96000))
        self.assertEqual(PARITY.BLOCKS, (17, 64, 511))
        self.assertEqual(len(PARITY.COVERAGE_CONTROLS), 35)
        self.assertEqual(PARITY.intended_coverage_cases(), 1890)

    def test_filter_sweeps_enable_the_filter_under_test(self) -> None:
        controls = tuple(item for item in PARITY.COVERAGE_CONTROLS
                         if item[1] in ("highPass", "filterEnabled", "cutoff", "resonance"))
        captured = []

        def render(**kwargs):
            captured.append(PARITY.native_state(
                kwargs["input_wav"], pad=kwargs["pad"], master=kwargs["master"]))
            kwargs["output_wav"].write_bytes(kwargs["input_wav"].read_bytes())
            return {"output_sha256": "fixture", "state_sha256": "fixture",
                    "host": {"tail_frames": 0}}

        with tempfile.TemporaryDirectory() as directory:
            work = Path(directory)
            source = work / "input.wav"
            PARITY.write_impulse_pcm24(source, frames=128)
            with patch.object(PARITY, "COVERAGE_CONTROLS", controls), \
                 patch.object(PARITY, "LEVELS", (0.5,)), \
                 patch.object(PARITY, "RATES", (48000,)), \
                 patch.object(PARITY, "BLOCKS", (64,)), \
                 patch.object(PARITY, "render_case", side_effect=render):
                report = PARITY.run_coverage(source, work, host=work / "host",
                    bundle=work / "plugin", plugin_sha256="fixture")

        self.assertEqual(len(captured), 7)
        for case, state in zip(report["cases"], captured):
            scope, control = case["scope"], case["control"]
            with self.subTest(scope=scope, control=control):
                pad = state["pads"][0]
                self.assertEqual(pad["hpfBypass"], control != "highPass")
                self.assertEqual(pad["filterEnabled"], scope == "pad" and control != "highPass")
                self.assertEqual(state["master"]["filterEnabled"], scope == "master")
        self.assertTrue(PARITY.REFERENCE_CASES["master_filter_035"]["master"]["filterEnabled"])

    def test_shifted_actual_render_fails_alignment_acceptance(self) -> None:
        dry, comparisons = self._passing_reference_measurements()
        reference = PARITY.np.random.default_rng(13).normal(size=(2048, 2))
        shifted_actual = PARITY.np.vstack(
            (PARITY.np.zeros((9, 2)), reference[:-9]))
        alignment = PARITY.align(reference, shifted_actual)
        comparisons["master_filter_035"]["alignment"] = alignment
        comparisons["master_filter_035"]["aligned_nrmse"] = PARITY.aligned_nrmse(
            reference, shifted_actual, int(alignment["lag_samples"]))

        acceptance = PARITY.evaluate_reference_acceptance(
            dry, comparisons, suite_mode="diagnostic")

        self.assertEqual(acceptance["result"], "fail")
        self.assertFalse(
            acceptance["checks"]["actual_alignment"]["master_filter_035"]["lag_samples"])

    def test_alignment_preserves_delay_sign_and_normalization(self) -> None:
        positions = PARITY.np.arange(13)
        signal = ((PARITY.np.sin(positions * 1.73)
                   + 0.31 * PARITY.np.cos(positions * 0.61))
                  * PARITY.np.geomspace(1.0e-5, 1.0, 13))
        reference = PARITY.np.column_stack((signal, signal))
        delayed = PARITY.np.vstack((PARITY.np.zeros((3, 2)), reference[:-3]))
        for first, second, expected_lag in (
                (reference, delayed, 3), (delayed, reference, -3)):
            with self.subTest(lag=expected_lag):
                alignment = PARITY.align(first, second, radius=5)
                self.assertEqual(alignment["lag_samples"], expected_lag)
                self.assertAlmostEqual(alignment["correlation"], 1.0, places=10)
                self.assertAlmostEqual(PARITY.aligned_nrmse(
                    first, second, int(alignment["lag_samples"])), 0.0, places=12)

    def test_stereo_measurements_do_not_erase_opposite_polarity_channels(self) -> None:
        positions = PARITY.np.arange(2048)
        signal = PARITY.np.sin(2.0 * PARITY.np.pi * positions / 16)
        in_phase = PARITY.np.column_stack((signal, signal))
        anti_phase = PARITY.np.column_stack((signal, -signal))
        for name, expected in PARITY.metrics(in_phase, 48000).items():
            self.assertAlmostEqual(PARITY.metrics(anti_phase, 48000)[name],
                                   expected, places=10, msg=name)
        self.assertEqual(PARITY.dominant_frequency_hz(anti_phase, 48000), 3000.0)
        alignment = PARITY.align(anti_phase, anti_phase, radius=5)
        self.assertEqual(alignment["lag_samples"], 0)
        self.assertAlmostEqual(alignment["correlation"], 1.0, places=10)

        impulse = PARITY.np.zeros((64, 2))
        impulse[9] = (0.5, -0.5)
        self.assertEqual(PARITY.onset_and_duration(impulse), (9, 1))
        self.assertEqual(PARITY.onset_and_duration(PARITY.np.zeros((64, 2))), (0, 0))

    def test_pitch_and_decay_measurements_preserve_stereo_energy(self) -> None:
        signal = PARITY.np.random.default_rng(17).normal(size=256)
        source = PARITY.np.column_stack((signal, -signal))
        positions = PARITY.np.arange(200) * 1.125
        effected = PARITY.np.column_stack([
            PARITY.np.interp(positions, PARITY.np.arange(len(source)), channel)
            for channel in source.T])
        self.assertAlmostEqual(PARITY.resampling_pitch_ratio(source, effected),
                               1.125, places=6)

        sample_rate = 1000
        times = PARITY.np.arange(1000) / sample_rate
        decay = PARITY.np.exp(-5.0 * times) * PARITY.np.cos(2.0 * PARITY.np.pi * 73 * times)
        in_phase = PARITY.np.column_stack((decay, decay))
        anti_phase = PARITY.np.column_stack((decay, -decay))
        expected = PARITY.ambience_measurement(in_phase, sample_rate)
        actual = PARITY.ambience_measurement(anti_phase, sample_rate)
        self.assertTrue(PARITY.math.isfinite(actual["rt60_seconds"]))
        for name, value in expected.items():
            self.assertAlmostEqual(actual[name], value, places=10, msg=name)

    def test_ambience_measurements_subtract_the_matching_dry_render(self) -> None:
        rate = 1000
        times = PARITY.np.arange(2000) / rate
        dry = PARITY.np.exp(-9.0 * times) * PARITY.np.cos(2.0 * PARITY.np.pi * 211 * times)
        tail = 0.1 * PARITY.np.exp(-2.0 * times) * PARITY.np.cos(2.0 * PARITY.np.pi * 37 * times)
        neutral = PARITY.np.column_stack((dry, -dry))
        wet = PARITY.np.column_stack((tail, tail))
        affected = neutral + wet
        expected = PARITY.ambience_measurement(wet, rate)
        actual = PARITY.ambience_return_measurement(affected, neutral, rate)
        for name, value in expected.items():
            self.assertAlmostEqual(actual[name], value, places=9, msg=name)
        raw = PARITY.ambience_measurement(affected, rate)
        self.assertNotAlmostEqual(raw["spectral_centroid_hz"], actual["spectral_centroid_hz"], places=1)

    def test_ambience_shape_verdict_is_invariant_to_return_gain(self) -> None:
        taps = {"room": {"room": 0.6, "reverb": 0.01},
                "reverb": {"room": 0.2, "reverb": 0.4}}
        rms = {"room": 0.3, "reverb": 0.5}
        expected = PARITY.ambience_tap_shape(taps, rms)
        self.assertTrue(all(expected["checks"].values()))
        for gain in (0.1, 10.0):
            scaled = copy.deepcopy(taps)
            scaled["reverb"] = {key: value * gain
                                for key, value in scaled["reverb"].items()}
            actual = PARITY.ambience_tap_shape(
                scaled, {**rms, "reverb": rms["reverb"] * gain})
            self.assertEqual(actual["checks"], expected["checks"])
            for effect, values in expected["normalized_tap_energy"].items():
                for tap, value in values.items():
                    self.assertAlmostEqual(actual["normalized_tap_energy"][effect][tap], value)
        reversed_taps = {"room": taps["reverb"], "reverb": taps["room"]}
        reversed_rms = {"room": rms["reverb"], "reverb": rms["room"]}
        self.assertFalse(any(PARITY.ambience_tap_shape(reversed_taps, reversed_rms)["checks"].values()))
        silent = {effect: {tap: 0.0 for tap in values} for effect, values in taps.items()}
        self.assertFalse(any(PARITY.ambience_tap_shape(silent, {"room": 0.0, "reverb": 0.0})["checks"].values()))

    def test_ambience_return_rejects_a_mismatched_neutral(self) -> None:
        with self.assertRaisesRegex(ValueError, "matching"):
            PARITY.ambience_return_measurement(
                PARITY.np.zeros((2000, 2)), PARITY.np.zeros((1000, 2)), 1000)

    def test_ambience_cases_match_original_pad_sends_and_master_returns(self) -> None:
        for effect in ("delay", "reverb", "room"):
            with self.subTest(effect=effect):
                case = PARITY.REFERENCE_CASES[f"master_{effect}_050"]
                self.assertEqual(case.get("pad", {}).get(effect), 0.5)
                self.assertEqual(case["master"][effect], 0.5)

    def test_waveform_wrong_actual_render_fails_alignment_acceptance(self) -> None:
        dry, comparisons = self._passing_reference_measurements()
        reference = PARITY.np.random.default_rng(21).normal(size=(2048, 2))
        waveform_wrong_actual = PARITY.np.random.default_rng(22).normal(size=(2048, 2))
        alignment = PARITY.align(reference, waveform_wrong_actual)
        comparisons["master_crush_080"]["alignment"] = alignment
        comparisons["master_crush_080"]["aligned_nrmse"] = PARITY.aligned_nrmse(
            reference, waveform_wrong_actual, int(alignment["lag_samples"]))

        acceptance = PARITY.evaluate_reference_acceptance(
            dry, comparisons, suite_mode="diagnostic")

        self.assertEqual(acceptance["result"], "fail")
        self.assertFalse(
            acceptance["checks"]["actual_alignment"]["master_crush_080"]["correlation"])

    def test_diagnostic_returns_cannot_claim_full_reference_acceptance(self) -> None:
        dry, comparisons = self._passing_reference_measurements()

        diagnostic = PARITY.evaluate_reference_acceptance(
            dry, comparisons, suite_mode="diagnostic")
        full = PARITY.evaluate_reference_acceptance(
            dry, comparisons, suite_mode="full")

        self.assertEqual(diagnostic["result"], "partial")
        self.assertEqual(diagnostic["suite_status"], "partial")
        self.assertEqual(full["result"], "fail")
        self.assertEqual(full["missing_categories"], ["ambience"])
        self.assertNotIn("ambience", full["available_categories"])

    def test_mapping_pcm_error_is_a_hard_failure(self) -> None:
        dry, comparisons = self._passing_reference_measurements()
        comparisons["pad_gain_050"]["maximum_sample_absolute_error"] = PARITY.PCM24_LSB * 2

        acceptance = PARITY.evaluate_reference_acceptance(
            dry, comparisons, suite_mode="full")

        self.assertEqual(acceptance["result"], "fail")
        self.assertFalse(acceptance["checks"]["mapping"]["pad_gain_050"]["sample_absolute_error"])

    def test_pan_transfer_allows_only_its_second_quantization_lsb(self) -> None:
        dry, comparisons = self._passing_reference_measurements()
        comparisons["pad_pan_075"]["maximum_sample_absolute_error"] = PARITY.PCM24_LSB * 2
        accepted = PARITY.evaluate_reference_acceptance(
            dry, comparisons, suite_mode="full")
        self.assertTrue(accepted["checks"]["mapping"]["pad_pan_075"]["sample_absolute_error"])
        self.assertEqual(accepted["suite_status"], "partial")

        comparisons["pad_pan_075"]["maximum_sample_absolute_error"] = PARITY.PCM24_LSB * 3
        rejected = PARITY.evaluate_reference_acceptance(
            dry, comparisons, suite_mode="full")
        self.assertEqual(rejected["result"], "fail")

    def test_diagnostic_ambience_is_not_used_as_a_gate(self) -> None:
        dry, comparisons = self._passing_reference_measurements()
        comparisons["master_delay_050"]["ambience"]["rt60_relative_error"] = 0.5

        acceptance = PARITY.evaluate_reference_acceptance(
            dry, comparisons, suite_mode="full")

        self.assertEqual(acceptance["result"], "fail")
        self.assertEqual(acceptance["missing_categories"], ["ambience"])
        self.assertNotIn("master_delay_050", acceptance["checks"]["ambience"])

    def test_missing_functional_ambience_measurement_cannot_pass(self) -> None:
        dry, comparisons = self._passing_reference_measurements()
        del comparisons["_ambience_functional"]

        acceptance = PARITY.evaluate_reference_acceptance(
            dry, comparisons, suite_mode="full")

        self.assertEqual(acceptance["result"], "fail")
        self.assertFalse(
            acceptance["checks"]["ambience"]["actual_vst3_functional"]["measurement_present"])

    def test_python_contains_no_reconstruction_transfer(self) -> None:
        source = (ROOT / "tools/measure_effect_parity.py").read_text(encoding="utf-8")
        self.assertNotIn("def compress(", source)
        self.assertNotIn("def crush(", source)
        self.assertNotIn("def low_pass(", source)
        self.assertIn("subprocess.run(command", source)

    @staticmethod
    def _passing_reference_measurements():
        feature_errors = {
            name: {feature: 0.0 for feature in limits}
            for name, limits in PARITY.LIMITS.items()
        }
        comparisons = {
            name: {
                "absolute_error": copy.deepcopy(feature_errors[name]),
                "alignment": {"lag_samples": 0, "correlation": 1.0},
                "aligned_nrmse": 0.0,
            }
            for name in PARITY.LIMITS
        }
        for name, case in PARITY.REFERENCE_CASES.items():
            if name == "dry" or name in comparisons:
                continue
            comparisons[name] = {
                "absolute_error": {},
                "alignment": {"lag_samples": 0, "correlation": 1.0},
                "aligned_nrmse": 0.0,
                "maximum_sample_absolute_error": 0.0,
                "peak_delta_db": 0.0,
                "rms_delta_db": 0.0,
            }
            if case["category"] == "timing-pitch":
                comparisons[name]["timing_pitch"] = {
                    "onset_error_samples": 0, "duration_error_samples": 0,
                    "pitch_error_cents": 0.0,
                }
            if case["category"] == "ambience":
                comparisons[name]["ambience"] = {
                    "windowed_decay_error_db": 0.0,
                    "rt60_relative_error": 0.0,
                    "centroid_relative_error": 0.0,
                }
        for name in ("pad_neutral", "pad_gain_050", "pad_pan_075"):
            comparisons[name].setdefault("maximum_sample_absolute_error", 0.0)
            comparisons[name].setdefault("peak_delta_db", 0.0)
            comparisons[name].setdefault("rms_delta_db", 0.0)
        comparisons["_ambience_functional"] = {
            "checks": {
                "delay_changes_output": True,
                "reverb_changes_output": True,
                "room_changes_output": True,
                "delay_quarter_second_return": True,
                "reverb_is_longer_than_room": True,
                "room_is_earlier_than_reverb": True,
            }
        }
        dry = {
            "alignment": {"lag_samples": 0, "correlation": 1.0},
            "aligned_nrmse": 0.0,
            "peak_absolute_error": 0.0,
            "rms_absolute_error": 0.0,
            "peak_delta_db": 0.0,
            "rms_delta_db": 0.0,
        }
        return dry, comparisons


if __name__ == "__main__":
    unittest.main()
