from __future__ import annotations

import importlib.util
import copy
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location(
    "measure_effect_parity", ROOT / "tools/measure_effect_parity.py")
assert SPEC is not None and SPEC.loader is not None
PARITY = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(PARITY)


class EffectParityHarnessTests(unittest.TestCase):
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
        self.assertFalse(state["global"]["sequencerEnabled"])
        for pad in state["pads"]:
            for effect in ("lpfBypass", "hpfBypass", "driveBypass", "crushBypass"):
                self.assertIs(pad[effect], False)

    def test_coverage_matrix_has_every_routed_control_and_grid_point(self) -> None:
        controls = {(scope, name) for scope, name, _ in PARITY.COVERAGE_CONTROLS}
        expected_pad = {
            "layerMix", "gain", "pan", "attackSeconds", "attackNormalized", "damp",
            "pitchSemitones", "fineCents", "highPass", "filterEnabled",
            "cutoff", "resonance", "driveEnabled", "driveGainDb",
            "distortionMix", "compression", "crush", "reverb", "room", "delay",
            "oneShot", "muted", "solo",
        }
        expected_master = {
            "filterEnabled", "cutoff", "resonance", "delay", "distortion",
            "reverb", "room", "crush", "compression", "gain",
        }
        self.assertEqual({name for scope, name in controls if scope == "pad"}, expected_pad)
        self.assertEqual({name for scope, name in controls if scope == "master"}, expected_master)
        self.assertEqual(PARITY.LEVELS, (0.0, 0.25, 0.5, 0.75, 0.8, 1.0))
        self.assertEqual(PARITY.RATES, (44100, 48000, 96000))
        self.assertEqual(PARITY.BLOCKS, (17, 64, 511))
        self.assertEqual(len(PARITY.COVERAGE_CONTROLS), 33)
        self.assertEqual(PARITY.intended_coverage_cases(), 1782)

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

    def test_full_reference_categories_can_claim_full_acceptance(self) -> None:
        dry, comparisons = self._passing_reference_measurements()

        diagnostic = PARITY.evaluate_reference_acceptance(
            dry, comparisons, suite_mode="diagnostic")
        full = PARITY.evaluate_reference_acceptance(
            dry, comparisons, suite_mode="full")

        self.assertEqual(diagnostic["result"], "pass")
        self.assertEqual(diagnostic["suite_status"], "full")
        self.assertEqual(full["result"], "pass")
        self.assertEqual(full["missing_categories"], [])

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
        self.assertEqual(accepted["result"], "pass")

        comparisons["pad_pan_075"]["maximum_sample_absolute_error"] = PARITY.PCM24_LSB * 3
        rejected = PARITY.evaluate_reference_acceptance(
            dry, comparisons, suite_mode="full")
        self.assertEqual(rejected["result"], "fail")

    def test_informational_unchanged_ambience_is_not_used_as_a_gate(self) -> None:
        dry, comparisons = self._passing_reference_measurements()
        comparisons["master_delay_050"]["ambience"]["rt60_relative_error"] = 0.5

        acceptance = PARITY.evaluate_reference_acceptance(
            dry, comparisons, suite_mode="full")

        self.assertEqual(acceptance["result"], "pass")
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
