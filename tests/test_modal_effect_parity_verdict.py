from __future__ import annotations

import importlib.util
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location(
    "modal_effect_parity_verdict", ROOT / "tools/modal_effect_parity_verdict.py")
assert SPEC is not None and SPEC.loader is not None
VERDICT = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(VERDICT)


def accepted_report() -> dict[str, object]:
    return {
        "references": {
            "acceptance": {"result": "pass", "suite_status": "full"},
        },
        "coverage": {"complete": True},
    }


class ModalEffectParityVerdictTests(unittest.TestCase):
    def test_rejects_nonzero_parity_process(self) -> None:
        self.assertIn("exit code 7", VERDICT.parity_failure(7, accepted_report()))

    def test_rejects_failed_or_partial_reference_acceptance(self) -> None:
        for acceptance in (
            {"result": "fail", "suite_status": "full"},
            {"result": "pass", "suite_status": "partial"},
        ):
            with self.subTest(acceptance=acceptance):
                report = accepted_report()
                report["references"]["acceptance"] = acceptance
                self.assertIn("reference acceptance", VERDICT.parity_failure(0, report))

    def test_rejects_incomplete_coverage(self) -> None:
        report = accepted_report()
        report["coverage"]["complete"] = False
        self.assertIn("coverage is incomplete", VERDICT.parity_failure(0, report))

    def test_accepts_only_complete_full_pass(self) -> None:
        self.assertIsNone(VERDICT.parity_failure(0, accepted_report()))


if __name__ == "__main__":
    unittest.main()
