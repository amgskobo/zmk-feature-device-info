#!/usr/bin/env python3
"""Full production handler, generated owner schema and real nanopb."""

from pathlib import Path
import argparse
from contextlib import nullcontext
import hashlib
import json
import os
import re
import shutil
import subprocess
import tempfile
import time

root = Path(__file__).resolve().parents[2]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument(
    "--source", type=Path, default=root / "src/studio/device_info_handler.c"
)
parser.add_argument(
    "--output",
    type=Path,
    help="Fresh directory retaining generated inputs, binaries and raw runs",
)
args = parser.parse_args()
zmk = Path(
    os.environ.get("ZMK_ROOT")
    or subprocess.check_output(
        ["west", "list", "zmk", "-f", "{abspath}"], text=True
    ).strip()
)
ble_header = zmk / "app/include/zmk/ble.h"
ble_source = ble_header.read_text()
profile_definitions = ble_source[
    ble_source.index("#define ZMK_BLE_IS_CENTRAL") : ble_source.index(
        "void zmk_ble_clear_bonds"
    )
]
nanopb = Path(
    os.environ.get("NANOPB_SOURCE")
    or subprocess.check_output(
        ["west", "list", "nanopb", "-f", "{abspath}"], text=True
    ).strip()
)
test_mode = os.environ.get("ZMK_TEST_MODE", "quick")
if test_mode not in ("quick", "full"):
    raise SystemExit("ZMK_TEST_MODE must be quick or full")
if args.output:
    args.output = args.output.parent.resolve() / args.output.name
    if os.path.lexists(args.output):
        raise FileExistsError(args.output)
    args.output.mkdir(parents=True, exist_ok=False)
    workspace = nullcontext(str(args.output))
else:
    workspace = tempfile.TemporaryDirectory(prefix="device-info-protocol-")


def run_recorded(command, name, work):
    start = time.time_ns()
    result = subprocess.run(
        command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True
    )
    (work / (name + ".log")).write_text(result.stdout)
    (work / (name + ".json")).write_text(
        json.dumps(
            dict(
                command=command,
                start_ns=start,
                end_ns=time.time_ns(),
                exit=result.returncode,
            ),
            indent=2,
        )
        + "\n"
    )
    print(result.stdout, end="", flush=True)
    result.check_returncode()


with workspace as folder:
    work = Path(folder)
    include = work / "include"
    generated = work / "generated"
    generated.mkdir()
    (generated / "selected_ble_profile_counts.h").write_text(profile_definitions)
    print(
        "Selected zmk/ble.h sha256="
        + hashlib.sha256(ble_header.read_bytes()).hexdigest(),
        flush=True,
    )
    for name in re.findall(r"#include <([^>]+)>", args.source.read_text()):
        if name.startswith("pb_") or name.endswith(".pb.h"):
            continue
        path = include / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text('#include "framework.h"\n')
    shutil.copytree(nanopb / "generator", work / "generator")
    run_recorded(
        [
            "python3",
            str(work / "generator/nanopb_generator.py"),
            "-I",
            str(root / "proto"),
            "-D",
            str(generated),
            str(root / "proto/zmk/device_info/device_info.proto"),
        ],
        "generate",
        work,
    )
    (work / "inputs.json").write_text(
        json.dumps(
            {
                str(path): hashlib.sha256(path.read_bytes()).hexdigest()
                for path in [
                    args.source.resolve(),
                    root / "tests/protocol/harness.c",
                    root / "tests/protocol/framework.h",
                    ble_header,
                    *sorted((root / "proto").rglob("*.*")),
                    *(
                        nanopb / name
                        for name in (
                            "pb_common.c",
                            "pb_encode.c",
                            "pb_decode.c",
                            "pb.h",
                            "pb_common.h",
                            "pb_encode.h",
                            "pb_decode.h",
                        )
                    ),
                ]
            },
            indent=2,
        )
        + "\n"
    )
    # Version strings: a dirty build longer than its 80-byte field (the
    # "-dirty" suffix is cut from the reported copy), a tag that merely
    # contains "dirty", and a clean module.
    versions = {
        "long-versions": [
            '-DZMK_BUILD_VERSION="v' + "x" * 84 + '-dirty"',
            '-DZMK_CONFIG_BUILD_VERSION="v1-dirtyfix"',
            '-DMODULE_BUILD_VERSION="module"',
            "-DEXPECT_ZMK_DIRTY=1",
            "-DEXPECT_CONFIG_DIRTY=0",
            "-DEXPECT_MODULE_DIRTY=0",
        ],
    }
    # Battery reporting differs from BLE in central-two-peers, so the flag
    # cannot pass by reading the BLE option instead.
    for role, split, central, hwinfo, secured, ble, profiles, peer_slots, battery in [
        ("standalone", 0, 0, 1, 1, 1, 5, 0, 1),
        ("central-one-peer", 1, 1, 1, 0, 1, 4, 1, 1),
        ("central-two-peers", 1, 1, 1, 1, 1, 3, 2, 0),
        ("peripheral", 1, 0, 0, 1, 1, 5, 0, 1),
        ("usb-only", 0, 0, 1, 1, 0, 0, 0, 0),
        ("long-versions", 0, 0, 1, 1, 1, 5, 0, 1),
        ("header-standalone", 0, 0, 1, 1, 1, 5, 0, 1),
        ("header-central", 1, 1, 1, 1, 1, 5, 1, 1),
    ]:
        defines = dict(
            ARCH_POSIX=1,
            HWINFO=hwinfo,
            ZMK_SPLIT=split,
            ZMK_SPLIT_ROLE_CENTRAL=central,
            ZMK_SPLIT_BLE=split,
            ZMK_SPLIT_BLE_CENTRAL_PERIPHERALS=peer_slots,
            ZMK_BLE=ble,
            ZMK_USB=1,
            ZMK_DISPLAY=1,
            ZMK_RGB_UNDERGLOW=0,
            ZMK_BACKLIGHT=0,
            ZMK_BATTERY_REPORTING=battery,
            ZMK_DEVICE_INFO_STUDIO_RPC_REQUIRE_UNLOCK=secured,
        )
        header_options = []
        if role.startswith("header-"):
            header_options = [
                "-DFIXTURE_USE_SELECTED_CORE_PROFILES=1",
                "-DCONFIG_BT_MAX_PAIRED=" + str(5 + peer_slots),
            ]
        for mode, options in [
            (
                "sanitized",
                [
                    "-O1",
                    "-g",
                    "-fsanitize=address,undefined",
                    "-fno-sanitize-recover=all",
                    "-fno-pie",
                    "-no-pie",
                ],
            ),
            ("optimized", ["-O2"]),
        ]:
            if mode == "optimized" and test_mode != "full":
                continue
            binary = work / (role + "-" + mode)
            command = [
                "cc",
                "-std=c11",
                "-DFIXTURE_BLE_PROFILE_COUNT=" + str(profiles),
                *header_options,
                *versions.get(role, []),
                "-Wall",
                "-Wextra",
                "-Werror",
                "-Wshadow",
                "-Wno-unused-parameter",
                "-Wno-unused-function",
                *options,
                '-DOWNER_HANDLER_SOURCE="' + str(args.source.resolve()) + '"',
                *(
                    "-DCONFIG_" + key + "=" + str(value)
                    for key, value in defines.items()
                ),
                "-I" + str(include),
                "-I" + str(root / "tests/protocol"),
                "-I" + str(generated),
                "-I" + str(nanopb),
                str(root / "tests/protocol/harness.c"),
                *map(str, generated.rglob("*.pb.c")),
                *(
                    str(nanopb / name)
                    for name in ("pb_common.c", "pb_encode.c", "pb_decode.c")
                ),
                "-o",
                str(binary),
            ]
            run_recorded(command, role + "-" + mode + "-compile", work)
            print("COMPILED", role, mode, flush=True)
            (work / (binary.name + ".sha256")).write_text(
                hashlib.sha256(binary.read_bytes()).hexdigest() + "\n"
            )
            run_recorded([str(binary)], role + "-" + mode + "-run", work)
            print(role, mode, "PASS", flush=True)
