import platform
import os
import struct
import tempfile
import subprocess
import unittest
from pathlib import Path

from dataclasses import dataclass

from elftools.elf.elffile import ELFFile
from elftools.elf.sections import NoteSection

from native_verifier import verify_native
from test_native_verifier import NativeVerifierContract as NativeVerifierContract

from test_build_budget import BuildBudgetContract as BuildBudgetContract

THIS_DIR = Path(__file__).resolve().parent.parent


def read_build_id(elf_path: Path) -> str | None:
    """Return the GNU build-id (hex) embedded in an ELF, or None if absent."""
    with open(elf_path, "rb") as f:
        elf = ELFFile(f)
        for section in elf.iter_sections():
            if not isinstance(section, NoteSection):
                continue
            for note in section.iter_notes():
                if note["n_type"] == "NT_GNU_BUILD_ID":
                    return note["n_desc"]
    return None


def read_build_id_range(elf_path: Path) -> tuple[int, int, int, bytes]:
    """Return the reader's boundary symbols, the note's address and its bytes."""
    with open(elf_path, "rb") as f:
        elf = ELFFile(f)
        section = elf.get_section_by_name(".note.gnu.build-id")
        symbols = elf.get_section_by_name(".symtab")
        start, end = (
            symbols.get_symbol_by_name(name)[0]["st_value"]
            for name in ("__build_id_note_start", "__build_id_note_end")
        )
        return start, end, section["sh_addr"], section.data()


def read_uf2(uf2_path: Path, address: int, size: int) -> bytes | None:
    """Return the bytes a UF2 image flashes at [address, address + size), or
    None when the image does not cover all of them."""
    data = uf2_path.read_bytes()
    image = {}
    for block in range(0, len(data), 512):
        target, length = struct.unpack_from("<II", data, block + 12)
        payload = data[block + 32 : block + 32 + length]
        image.update(zip(range(target, target + length), payload))
    if not all(at in image for at in range(address, address + size)):
        return None
    return bytes(image[at] for at in range(address, address + size))


def run_west(args: list[str]) -> subprocess.CompletedProcess[str]:
    if args and args[0] == "zmk-build":
        parallelism = int(os.environ.get("OWNER_TEST_BUILD_PARALLELISM", "1"))
        if parallelism < 1:
            raise ValueError("OWNER_TEST_BUILD_PARALLELISM must be a positive integer")
        args = [args[0], "--parallelism", str(parallelism), *args[1:]]
    return subprocess.run(
        ["west", *args],
        capture_output=True,
        text=True,
        cwd=THIS_DIR,
    )


@dataclass
class NotFound:
    text: str


@dataclass
class ConfigAndDeviceTree:
    # Expected rows in .config
    config: list[str | NotFound]
    # Expected rows in devicetree_generated.h
    device: list[str | NotFound]


class WestCommandsTests(unittest.TestCase):
    WEST_TOPDIR: Path
    BUILD_DIR: Path

    @classmethod
    def setUpClass(cls):
        cls.WEST_TOPDIR = Path(run_west(["topdir"]).stdout.strip())
        requested_output = os.environ.get("OWNER_TEST_OUTPUT")
        if requested_output:
            cls.BUILD_DIR = Path(requested_output).resolve()
            cls.BUILD_DIR.mkdir(parents=True, exist_ok=False)
        else:
            cls.BUILD_DIR = Path(tempfile.mkdtemp(prefix=THIS_DIR.name + "-tests-"))
        print(f"Owner test output: {cls.BUILD_DIR}", flush=True)

    @unittest.skipUnless(
        platform.system() == "Linux", "zmk-test is only supported on Linux"
    )
    def test_zmk_test(self):
        test_build_dir = self.BUILD_DIR / THIS_DIR.name

        result = run_west(["zmk-test", "tests", "-m", ".", "-d", str(test_build_dir)])
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        verify_native(test_build_dir, THIS_DIR / "tests")
        self.assertIn("PASS: test", result.stdout, result.stdout + result.stderr)
        self.assertIn("PASS: studio", result.stdout, result.stdout + result.stderr)
        self.assertIn(
            "PASS: studio_unsecured", result.stdout, result.stdout + result.stderr
        )
        self.assertNotIn("FAILED: ", result.stdout, result.stdout + result.stderr)

    @unittest.skipUnless(platform.system() == "Linux", "host build requires Linux")
    def test_protocol(self):
        result = subprocess.run(
            [
                "python3",
                str(THIS_DIR / "tests" / "protocol" / "run.py"),
                "--output",
                str(self.BUILD_DIR / "protocol"),
            ],
            env={**os.environ, "ZMK_TEST_MODE": "full"},
            capture_output=True,
            text=True,
            cwd=THIS_DIR,
        )
        (self.BUILD_DIR / "test_protocol.log").write_text(
            result.stdout + result.stderr, encoding="utf-8"
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    @unittest.skipUnless(
        platform.system() == "Linux", "native readiness requires Linux"
    )
    def test_readiness_encoding(self):
        result = subprocess.run(
            [
                "python3",
                "tests/readiness/run.py",
                "--output",
                str(self.BUILD_DIR / "readiness"),
            ],
            cwd=THIS_DIR,
            capture_output=True,
            text=True,
        )
        (self.BUILD_DIR / "test_readiness.log").write_text(
            result.stdout + result.stderr
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_zmk_build(self):
        self._test_zmk_build(
            {
                "board_feature_disabled": ConfigAndDeviceTree(
                    config=[
                        'CONFIG_ZMK_KEYBOARD_NAME="Module Test"',
                        "CONFIG_ZMK_USB=y",
                        "CONFIG_ZMK_BLE=y",
                        "# CONFIG_ZMK_DEVICE_INFO is not set",
                    ],
                    device=[
                        "DT_COMPAT_HAS_OKAY_zmk_keymap",
                    ],
                ),
                "board_with_device_info_rpc": ConfigAndDeviceTree(
                    config=[
                        "CONFIG_ZMK_STUDIO=y",
                        "CONFIG_ZMK_DEVICE_INFO=y",
                        "CONFIG_ZMK_DEVICE_INFO_STUDIO_RPC=y",
                        # A Studio unlock is required to read device info by default.
                        "CONFIG_ZMK_DEVICE_INFO_STUDIO_RPC_REQUIRE_UNLOCK=y",
                    ],
                    device=[],
                ),
                "board_without_rpc": ConfigAndDeviceTree(
                    config=[
                        "CONFIG_ZMK_DEVICE_INFO=y",
                        "# CONFIG_ZMK_STUDIO is not set",
                        NotFound("CONFIG_ZMK_DEVICE_INFO_STUDIO_RPC"),
                    ],
                    device=[],
                ),
            }
        )

        # The RPC feature enables a GNU build-id so a running device can be
        # matched back to its ELF; it must be present (SHA1 => 40 hex chars) in
        # that artifact and absent when the RPC feature is off.
        rpc_elf = self.BUILD_DIR / "board_with_device_info_rpc" / "zephyr" / "zmk.elf"
        self.assertTrue(rpc_elf.exists(), f"{rpc_elf} is missing")
        rpc_build_id = read_build_id(rpc_elf)
        self.assertIsNotNone(
            rpc_build_id, "build-id note missing from RPC firmware ELF"
        )
        self.assertEqual(
            len(rpc_build_id), 40, f"expected SHA1 build-id, got: {rpc_build_id}"
        )
        # The runtime reader takes the note from the boundary symbols, so they
        # must bracket exactly that section, and the flashed image must hold
        # the note at that address.
        start, end, address, note = read_build_id_range(rpc_elf)
        self.assertEqual((start, end), (address, address + len(note)))
        self.assertEqual(
            read_uf2(rpc_elf.with_name("zmk.uf2"), start, end - start),
            note,
            "build-id note is not in the flashed image",
        )

        no_rpc_elf = self.BUILD_DIR / "board_without_rpc" / "zephyr" / "zmk.elf"
        self.assertTrue(no_rpc_elf.exists(), f"{no_rpc_elf} is missing")
        self.assertIsNone(
            read_build_id(no_rpc_elf),
            "build-id note should be absent when RPC feature is disabled",
        )

    def _test_zmk_build(
        self, artifacts_and_expected_build_params: dict[str, ConfigAndDeviceTree]
    ):

        result = run_west(
            [
                "zmk-build",
                "tests/zmk-config",
                "-q",
                "-P",
                "1",
                "-d",
                str(self.BUILD_DIR),
            ]
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

        for artifact, entries in artifacts_and_expected_build_params.items():
            artifact_dir = self.BUILD_DIR / artifact / "zephyr"
            config_path = artifact_dir / ".config"
            device_tree_path = (
                artifact_dir
                / "include"
                / "generated"
                / "zephyr"
                / "devicetree_generated.h"
            )
            self._test_strings_in_file(
                config_path, entries.config, f"{artifact} config"
            )
            if entries.device:
                self._test_strings_in_file(
                    device_tree_path, entries.device, f"{artifact} device tree"
                )
            self.assertTrue(
                (artifact_dir / "zmk.uf2").exists(),
                f"{artifact} zmk.uf2 is missing in {artifact_dir}",
            )

    def _test_strings_in_file(
        self, file_path: Path, expected_strings: list[str | NotFound], hint: str
    ):
        self.assertTrue(file_path.exists(), f"{hint}: {file_path} is missing")
        file_text = file_path.read_text()

        for expected in expected_strings:
            if isinstance(expected, NotFound):
                if expected.text in file_text:
                    self.fail(
                        f"{hint}: {expected.text} found in {file_path}, but it should not be present"
                    )
            else:
                if expected not in file_text:
                    self.fail(f"{hint}: {expected} not found in {file_path}")


if __name__ == "__main__":
    unittest.main()
