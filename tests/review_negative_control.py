"""Restore the readiness omission in an isolated container source copy."""

import argparse
from pathlib import Path
import re


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("isolated_owner", type=Path)
    args = parser.parse_args()
    owner = Path(__file__).resolve().parents[1]
    isolated = args.isolated_owner.resolve()
    if isolated == owner or isolated.is_relative_to(owner):
        parser.error("Use an isolated container source copy")
    options = isolated / "proto/zmk/device_info/device_info.options"
    text = options.read_text()
    text, count = re.subn(
        r"^zmk\.device_info\.ZephyrDevice\.ready\s+proto3:false\s*$",
        "",
        text,
        flags=re.M,
    )
    if count != 1:
        parser.error("Expected exactly one explicit readiness presence option")
    handler = isolated / "src/studio/device_info_handler.c"
    source, count = re.subn(r"\bdev\.has_ready\s*=\s*true\s*;", "", handler.read_text())
    if count != 1:
        parser.error("Expected exactly one readiness presence assignment")
    options.write_text(text)
    handler.write_text(source)
    print("Applied isolated readiness-omission negative control")


if __name__ == "__main__":
    main()
