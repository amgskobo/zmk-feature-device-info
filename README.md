# ZMK Feature: Device Info

## Reviewed fork

This fork's reviewed source is on `codex/device-info-review` in
[`amgskobo/zmk-feature-device-info`](https://github.com/amgskobo/zmk-feature-device-info/tree/codex/device-info-review).
Select that repository URL and revision in the module's west project entry.
Keep the module's documented imports enabled where required. The original
upstream installation examples below describe the upstream repository.

![ZMK Version](https://img.shields.io/badge/ZMK-master-blue)
[![Test](https://github.com/cormoran/zmk-feature-device-info/actions/workflows/zmk-module.yml/badge.svg?branch=main)](https://github.com/cormoran/zmk-feature-device-info/actions/workflows/zmk-module.yml)

A ZMK module that exposes keyboard diagnostic information via the **unofficial** custom ZMK Studio RPC protocol. The information is displayed in a web UI and is intended to help end-users troubleshoot their keyboard firmware.

## What information is shown

| Section | Contents |
|---------|----------|
| **Build** | ZMK git hash, zmk-config git hash, this module's git hash, Zephyr version, build timestamp, board name, and a **build hash**. Each version shows a "dirty" warning if the source had uncommitted changes at build time (a `git describe --dirty` string ending in `-dirty`). |
| **Hardware** | MCU hardware ID (uniqueness depends on the hardware), reset cause read at boot (Power-On / Watchdog / Brownout / etc.; the hardware bits accumulate until a module such as zmk-feature-watchdog clears them), flash and SRAM size. |
| **ZMK Configuration** | KScan driver type, BLE/USB/split/display/RGB/backlight flags. |
| **Runtime** | Uptime in milliseconds, encoded as uint32; it wraps after about 49.7 days. |
| **Zephyr Devices** | All statically-declared Zephyr devices and whether each initialized successfully. Failing devices are shown first. |

A **Copy JSON** button lets users paste the full diagnostic data into a bug report.

The report includes the MCU's hardware ID, build/configuration identity
and device names. Review the JSON before sharing it and replace identifying
values with clearly labelled synthetic values when they are not needed for the
report. Copy JSON writes the received report to the clipboard only when clicked;
the application does not upload it. The Studio SDK can log connection/stream
errors to the browser console, so review those logs separately before sharing.

Device readiness is observed as each entry is encoded. Its boolean is always
present on the wire, including `false`, so a deferred device initialization
during response encoding cannot change the enclosing message lengths.

Each string has a fixed nanopb field limit; the repeated device list consists of
the devices statically compiled into that firmware. It is streamed through the
core's response encoder. Device Info has no mutable settings or subscriptions.
The selected DYA core serializes handler/response use and owns the transport and
Studio lock lifecycle. The configured unlock check occurs at dispatch; this
read-only handler does not add a second session or persistence owner.

The handler fills DYA's cleared, persistent response buffer directly. Success
and error replies avoid temporary full-message copies on the RPC thread stack;
the persistent buffer remains necessary because encoding follows the handler's
return. Field capacities, device enumeration and response lifetime are unchanged.

## Module User Guide

### 1. Add to `config/west.yml`

This module requires a patched ZMK with custom Studio RPC support.

```yml
manifest:
  remotes:
    - name: cormoran
      url-base: https://github.com/cormoran
  projects:
    - name: zmk-feature-device-info
      remote: cormoran
      revision: main
    - name: zmk
      remote: cormoran
      revision: main+custom-studio-protocol
      import:
        file: app/west.yml
```

### 2. Enable in `config/<shield>.conf`

```conf
CONFIG_ZMK_DEVICE_INFO=y

# Enable the web UI RPC (requires ZMK Studio)
CONFIG_ZMK_STUDIO=y
CONFIG_ZMK_DEVICE_INFO_STUDIO_RPC=y

# Optional: allow reading device info without unlocking Studio first.
# By default a Studio unlock is required before any device info can be read.
# CONFIG_ZMK_DEVICE_INFO_STUDIO_RPC_REQUIRE_UNLOCK=n
```

### 3. Open the web UI

Connect your keyboard via USB and open:
`https://cormoran.github.io/zmk-feature-device-info/`

Or run locally: see [web/README.md](./web/README.md).

## Notes

- **zmk-config version**: The module automatically reads the git hash of your zmk-config directory at build time (via the `ZMK_CONFIG` CMake variable set by `west build`). No extra setup is needed. It is empty when that directory is not in a git checkout; the module version reads `unknown` in the same case.
- **When versions are read**: The version strings and build timestamp are read when CMake configures the build. An incremental `west build` that does not reconfigure keeps the earlier values; use a pristine build (`west build -p`) for release images. The build-id below comes from the linked artifact.
- **Hardware ID / reset cause**: Requires `CONFIG_HWINFO=y` (enabled automatically when supported by the board). Zephyr's device ID API returns a hardware-dependent byte sequence and does not guarantee uniqueness across supported hardware.
- **BLE profile count**: Reports `ZMK_BLE_PROFILE_COUNT`, the selectable host profiles. Bond capacity reserved for split peers is excluded.
- **Build hash**: A GNU build-id is embedded in flash and reported here to help select the corresponding ELF for crash/error analysis. It is a linker-generated identifier, not a checksum of the complete ELF file. Keep and verify the artifact's full SHA-256 separately. Find its build-id with:

  ```bash
  readelf -n build/zephyr/zmk.elf   # look for the "Build ID" line
  ```

  The module enables the build-id automatically (Zephyr disables it by default) whenever the Studio RPC feature is on; no configuration is required.

## Development

See [the owner test guide](tests/README.md) for current commands, dependency
pins and verification limits, and [the Web guide](web/README.md) for the client.

```bash
# Run unit test + build test
python3 -m unittest discover -v -s tests -p test_owner.py

# Run web tests
bash tests/run-web.sh
```
