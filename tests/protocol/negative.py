#!/usr/bin/env python3
"""Directed owner-source faults; compile errors never count as behavioral detection."""

import argparse
import hashlib
import json
import os
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    out = args.output.resolve()
    out.mkdir(parents=True, exist_ok=False)
    plan = json.loads((ROOT / "tests/protocol/negative-cases.json").read_text())
    original = (ROOT / plan["source"]).read_text()
    results = []
    for index, case in enumerate(
        [dict(name="baseline")] + plan["cases"] + [dict(name="baseline-after")]
    ):
        folder = out / str(index)
        folder.mkdir()
        source = folder / "handler.c"
        text = original
        if "old" in case:
            if text.count(case["old"]) != 1:
                raise AssertionError("Ambiguous source fault: " + case["name"])
            text = text.replace(case["old"], case["new"])
        source.write_text(text)
        command = [
            sys.executable,
            str(ROOT / "tests/protocol/run.py"),
            "--source",
            str(source),
            "--output",
            str(folder / "build"),
        ]
        with (folder / "run.log").open("wb") as log:
            result = subprocess.run(
                command,
                stdout=log,
                stderr=subprocess.STDOUT,
                env=dict(os.environ, ZMK_TEST_MODE="quick"),
            )
        log = (folder / "run.log").read_text(errors="replace")
        baseline = "old" not in case
        detected = (
            result.returncode == 0
            if baseline
            else result.returncode != 0
            and "COMPILED" in log
            and (
                "Assertion" in log
                or "runtime error:" in log
                or "AddressSanitizer" in log
            )
        )
        row = dict(
            name=case["name"],
            exit=result.returncode,
            passed=detected,
            command=command,
            source_sha256=hashlib.sha256(source.read_bytes()).hexdigest(),
        )
        results.append(row)
        (out / "results.json").write_text(json.dumps(results, indent=2))
        print(case["name"], "PASS" if detected else "FAIL", flush=True)
        if not detected:
            raise SystemExit("Invalid/surviving source fault; see " + str(folder))


if __name__ == "__main__":
    main()
