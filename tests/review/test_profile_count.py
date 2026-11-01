"""Actual device-info handler and nanopb response across split role variants."""

import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


def function(path, name):
    text = path.read_text()
    for match in re.finditer(
        r"^(?:static )?[^\n;{}]*\b" + re.escape(name) + r"\(", text, re.M
    ):
        closing = text.index(")", match.end())
        if not text[closing + 1 :].lstrip().startswith("{"):
            continue
        opening = text.index("{", closing)
        depth = 0
        for token in re.finditer(
            r'/\*.*?\*/|//[^\n]*|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'|[{}]',
            text[opening:],
            re.S,
        ):
            if token.group() == "{":
                depth += 1
            elif token.group() == "}":
                depth -= 1
                if depth == 0:
                    start = match.start()
                    if not text[start : match.end() - len(name) - 1].strip():
                        start = text.rfind("\n", 0, start - 1) + 1
                    line = text.count("\n", 0, start) + 1
                    return (
                        f'#line {line} "{path}"\n'
                        + text[start : opening + token.end()]
                        + "\n"
                    )
    raise ValueError("missing function: " + name)


PREFIX = r"""
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <pb_encode.h>
#include <pb_decode.h>
#include "device_info.pb.h"
#define IS_ENABLED(x) (x)
#define CONFIG_HWINFO 0
#define CONFIG_ZMK_BLE 1
#define CONFIG_ZMK_USB 0
#define CONFIG_ZMK_DISPLAY 0
#define CONFIG_ZMK_RGB_UNDERGLOW 0
#define CONFIG_ZMK_BACKLIGHT 0
#define CONFIG_ZMK_BATTERY_LEVEL_FETCHING 0
#define CONFIG_ZMK_SPLIT_BLE_CENTRAL_PERIPHERALS 1
#define CONFIG_FLASH_SIZE 1024
#define CONFIG_SRAM_SIZE 256
#define ZMK_BUILD_VERSION "review"
#define ZMK_CONFIG_BUILD_VERSION "review"
#define MODULE_BUILD_VERSION "review"
#define KERNEL_VERSION_STRING "3.5.0"
#define BUILD_TIMESTAMP "2026-10-31T00:00:00Z"
#define CONFIG_BOARD "review"
#define DT_HAS_CHOSEN(x) 0
static int64_t k_uptime_get(void) { return 12345; }
static void fill_build_hash(char *out,size_t size) { if(size)out[0]=0; }
static bool encode_zephyr_devices(pb_ostream_t *s,const pb_field_t *f,void *const *arg) { (void)s;(void)f;(void)arg;return true; }
"""

SCENARIO = r"""
#line 1 "review-device-profile-count.c"
int main(void) {
    setbuf(stdout,NULL);
    zmk_device_info_GetDeviceInfoRequest req=zmk_device_info_GetDeviceInfoRequest_init_zero;
    zmk_device_info_Response resp=zmk_device_info_Response_init_zero;
    assert(handle_get_device_info(&req,&resp)==0);
    uint8_t bytes[2048];pb_ostream_t out=pb_ostream_from_buffer(bytes,sizeof(bytes));
    assert(pb_encode(&out,zmk_device_info_Response_fields,&resp));
    zmk_device_info_Response decoded=zmk_device_info_Response_init_zero;
    pb_istream_t in=pb_istream_from_buffer(bytes,out.bytes_written);
    assert(pb_decode(&in,zmk_device_info_Response_fields,&decoded));
    const zmk_device_info_DeviceInfoResponse *info=&decoded.response_type.device_info;
    assert(info->has_build && info->has_hardware && info->has_zmk_config && info->has_runtime);
    printf("split central=%d, paired slots=%d, host profiles=%d, reported=%u\n",CONFIG_ZMK_SPLIT_ROLE_CENTRAL,CONFIG_BT_MAX_PAIRED,ZMK_BLE_PROFILE_COUNT,info->zmk_config.ble_profile_count);
    assert(info->zmk_config.ble_profile_count==5);
    assert(info->runtime.uptime_ms==12345);
    return 0;
}
"""


class ProfileCountReview(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory(prefix="device-profile-review-")
        cls.addClassCleanup(cls.temp.cleanup)
        work = Path(cls.temp.name)
        nanopb = work / "nanopb"
        shutil.copytree(
            os.environ["NANOPB_ROOT"],
            nanopb,
            ignore=shutil.ignore_patterns(".git", "__pycache__"),
        )
        generated = work / "generated"
        generated.mkdir()
        subprocess.run(
            [
                "python3",
                str(nanopb / "generator/nanopb_generator.py"),
                "-D",
                str(generated),
                "-I",
                str(ROOT / "proto"),
                str(ROOT / "proto/zmk/device_info/device_info.proto"),
            ],
            check=True,
        )
        (header,) = generated.rglob("device_info.pb.h")
        (descriptor,) = generated.rglob("device_info.pb.c")
        ble = (Path(os.environ["ZMK_ROOT"]) / "app/include/zmk/ble.h").read_text()
        definitions = ble[
            ble.index("#define ZMK_BLE_IS_CENTRAL") : ble.index(
                "void zmk_ble_clear_bonds"
            )
        ]
        path = work / "review.c"
        path.write_text(
            PREFIX
            + definitions
            + function(
                ROOT / "src/studio/device_info_handler.c", "handle_get_device_info"
            )
            + SCENARIO
        )
        cls.binaries = {}
        for central in (0, 1):
            binary = work / str(central)
            subprocess.run(
                [
                    "cc",
                    "-std=c11",
                    "-O1",
                    "-g",
                    "-fsanitize=address,undefined",
                    "-fno-sanitize-recover=all",
                    "-DPB_WITHOUT_64BIT=1",
                    f"-DCONFIG_ZMK_SPLIT_ROLE_CENTRAL={central}",
                    f"-DCONFIG_ZMK_SPLIT_BLE={central}",
                    f"-DCONFIG_ZMK_SPLIT={central}",
                    f"-DCONFIG_BT_MAX_PAIRED={5 + central}",
                    "-I" + str(nanopb),
                    "-I" + str(generated),
                    "-I" + str(header.parent),
                    str(path),
                    str(descriptor),
                    str(nanopb / "pb_common.c"),
                    str(nanopb / "pb_encode.c"),
                    str(nanopb / "pb_decode.c"),
                    "-o",
                    str(binary),
                ],
                check=True,
            )
            cls.binaries[central] = binary

    def case(self, central):
        result = subprocess.run(
            [str(self.binaries[central])],
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
        )
        print(result.stdout, flush=True)
        self.assertEqual(result.returncode, 0, result.stdout)

    def test_nonsplit_profile_count_control(self):
        self.case(0)

    def test_split_profiles_exclude_peripheral_bonds(self):
        self.case(1)
