#!/usr/bin/env python3
"""Pure acceptance verdict for the Modal parity runner."""

from __future__ import annotations

from typing import Any


def parity_failure(returncode: int, report: dict[str, Any]) -> str | None:
    if returncode != 0:
        return f"parity process exited with exit code {returncode}"
    acceptance = report.get("references", {}).get("acceptance", {})
    if (acceptance.get("result") != "pass"
            or acceptance.get("suite_status") != "full"):
        return (
            "reference acceptance is not pass/full: "
            f"result={acceptance.get('result')!r}, "
            f"suite_status={acceptance.get('suite_status')!r}")
    if report.get("coverage", {}).get("complete") is not True:
        return "coverage is incomplete"
    return None
