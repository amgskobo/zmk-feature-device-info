# Device Info Web UI

This React client reads keyboard diagnostics through the `zmk__device_info`
custom Studio RPC subsystem. The current connection screen offers **Connect
Serial**. Use the keyboard's Studio serial interface and firmware with
`CONFIG_ZMK_DEVICE_INFO_STUDIO_RPC=y`. Studio unlock is required by default;
the firmware's explicit unsecured option is described in the
[module guide](../README.md).

The view shows build/version identity, hardware/reset information, selected
configuration, uint32 uptime and static Zephyr device readiness. Copy JSON
exports the received diagnostic information. A GNU build-id helps select an
ELF; verify the complete retained artifact's SHA-256 independently. Device
readiness and a successful diagnostic request are not a stability verdict.

## Current sources

- [App.tsx](src/App.tsx) contains connection controls and `DeviceInfoPanel`.
- The wire schema is [device_info.proto](../proto/zmk/device_info/device_info.proto).
- `buf.gen.yaml` generates ignored TypeScript messages under
  `src/proto/zmk/device_info/`. Do not edit generated files by hand.
- Executable tests, setup and Jest configuration live in `../tests/web/`.
  The application and installed packages remain in `web/`.

## Development

Use the checked-in lockfile when explicitly preparing dependencies:

```sh
# From web/
npm ci
npm run dev
npm run build
npm run lint
```

Development and builds generate TypeScript messages before use. Testing,
watch and coverage commands use the same generation step:

```sh
# From the owner root
bash tests/run-web.sh

# From web/
npm run test:watch
npm run test:coverage
```

The two existing UI suites retain seven tests for rendering, Serial connection,
missing context/subsystem and the diagnostic panel. They use controlled Studio
helpers. The separate firmware protocol suite validates real generated nanopb
responses; neither suite establishes physical USB/BLE, HWINFO driver behavior,
reset-cause accuracy or device uptime on a board. See the
[owner test guide](../tests/README.md) for commands and dependency pins.

Building local output does not publish the application.

## Dependency security

`package.json` overrides the Studio client's transitive `protobufjs` dependency
to `7.6.5`, including the patched UTF-8 helper. Use `npm ci` with this lockfile.
After changing dependencies, run `npm audit --omit=dev`, the Web tests and the
production build.
