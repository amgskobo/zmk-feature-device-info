#!/usr/bin/env python3
"""Execute real deferred-init/core-codec regression with the original Studio trace."""

import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tests"))
from native_verifier import verify_native  # noqa: E402 - standalone owner helper path

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--output", type=Path, required=True)
parser.add_argument(
    "--expect-encoding-failure",
    action="store_true",
    help="Isolated negative control only: require the readiness transition assertion to fail in three runs",
)
parser.add_argument(
    "--module-root",
    type=Path,
    default=ROOT,
    help="Explicit module under test; default is this owner",
)
args = parser.parse_args()
output = args.output.resolve()
output.mkdir(parents=True, exist_ok=False)
# The pinned core runner derives case names from the final /tests/ component.
fixtures = output / "tests"
case = fixtures / "studio_readiness"
shutil.copytree(ROOT / "tests/studio", case)
shutil.copy2(ROOT / "tests/test.dtsi", fixtures / "test.dtsi")
shutil.copy2(
    ROOT / "tests/readiness/required_hooks.json", fixtures / "required_hooks.json"
)
keymap = case / "native_sim.keymap"
keymap.write_text(
    keymap.read_text() + "\n" + (ROOT / "tests/readiness/deferred.overlay").read_text()
)
module = args.module_root.resolve(strict=True)
(case / "extra-cmake-args").write_text(
    "-DZMK_EXTRA_MODULES=" + str(module) + ";" + str(ROOT / "tests/readiness") + "\n"
)
command = [
    "west",
    "zmk-test",
    str(fixtures),
    "-m",
    str(module),
    "-d",
    str(output / "build"),
]
env = dict(os.environ, J=os.environ.get("J", "1"))
facts = {"command": command, "cwd": str(ROOT), "start": time.time()}
with (output / "build.log").open("x") as log:
    result = subprocess.run(
        command, cwd=ROOT, env=env, stdout=log, stderr=subprocess.STDOUT
    )
facts.update(exit=result.returncode, end=time.time())
(output / "command.json").write_text(json.dumps(facts, indent=2) + "\n")
if not args.expect_encoding_failure:
    assert result.returncode == 0, (
        "Readiness build/first execution failed; retain " + str(output / "build.log")
    )
binaries = list((output / "build").rglob("zmk.exe"))
assert len(binaries) == 1, binaries
if args.expect_encoding_failure:
    assert module != ROOT and not module.is_relative_to(ROOT), (
        "Use an isolated module variant"
    )
    binary = binaries[0].resolve()
    binary_hash = hashlib.sha256(binary.read_bytes()).hexdigest()
    records = []
    for repeat in range(1, 4):
        directory = output / "negative-runs" / str(repeat)
        directory.mkdir(parents=True)
        run = subprocess.run(
            [str(binary)],
            cwd=directory,
            env=env,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
            timeout=30,
        )
        (directory / "raw.log").write_text(run.stdout)
        transition = (
            "Device Info readiness deferred-init-during-output encoded=0" in run.stdout
        )
        expected_assertion = "encoded && stream.bytes_written == out.used" in run.stdout
        passed = run.returncode != 0 and transition and expected_assertion
        record = {
            "command": [str(binary)],
            "exit": run.returncode,
            "binary_sha256": binary_hash,
            "expected_failure": passed,
        }
        (directory / "command.json").write_text(json.dumps(record, indent=2) + "\n")
        records.append(record)
    unchanged = hashlib.sha256(binary.read_bytes()).hexdigest() == binary_hash
    (output / "negative-results.json").write_text(
        json.dumps({"runs": records, "binary_unchanged": unchanged}, indent=2) + "\n"
    )
    assert unchanged and all(record["expected_failure"] for record in records), (
        "Expected the readiness encoding assertion in every run"
    )
    print("PASS: readiness omission detected by the encoding assertion, repeats=3")
    raise SystemExit(0)
verify_native(output / "build", fixtures)
print(
    "PASS: real deferred-init during sizing/output, decoded device order/names/readiness, unchanged Studio trace, three identical binary runs"
)
