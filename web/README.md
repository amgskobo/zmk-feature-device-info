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

The original seven UI scenarios and eight provider lifecycle scenarios cover
rendering, Serial connection, missing context/subsystem, connection/index changes
and retired refresh results. They use controlled Studio helpers. SDK privacy
checks parse the actual shipped request/response codec methods resolved from
both the application and React hook, including a nested SDK when installed;
named request/response logging controls must fail the specific console-reference
check. Connection/stream error logging remains outside that codec check. This is
a source boundary check, not a general data-flow or browser-console guarantee.
The separate firmware protocol suite validates real generated nanopb
responses; neither suite establishes physical USB/BLE, HWINFO driver behavior,
reset-cause accuracy or device uptime on a board. See the
[owner test guide](../tests/README.md) for commands and dependency pins.

Building local output does not publish the application.

## Dependency security

`package.json` overrides the Studio client's transitive `protobufjs` dependency
to `7.6.5`, including the patched UTF-8 helper. Use `npm ci` with this lockfile.
Preparation must complete the installed packages' normal lifecycle scripts; if
npm reports pending script approvals, inspect those scripts and explicitly
prepare them before the network-free test/build run. Do not count an incomplete
install as successful preparation.

The 2026-11-07 lock review updated only advisory-affected compatible transitive
packages and their required dependency closure. For example, Handlebars 4.7.10
fixes the [reported AST injection issue](https://github.com/handlebars-lang/handlebars.js/security/advisories/GHSA-8r5x-fm3f-whwj).
Direct requirements, both resolved Studio Git commits, the Vite alias and
`protobufjs` override remain unchanged. The installed-graph audit reports zero
production vulnerabilities with `npm audit --omit=dev`; the full audit still
reports 18 development findings (14 moderate and four low). `deepmerge`, `glob`
and `minimatch` remain constrained by incoming development dependencies; the
suggested major substitutions/Jest downgrade were not applied. This is an
explicit remaining full-audit failure, not a claim that all dependencies are
secure. Recheck both audit scopes after dependency changes, then run the Web
tests, production build and lint with the reviewed lockfile.
