# Owner tests

This repository owns its executable test logic and fixtures under `tests/`.

`tests/web/lifecycle.spec.tsx` adds eight actual-provider/deferred-response
scenarios to the original seven Web scenarios. An old connection's manual
refresh success/error may not replace a newer connection's information. Old
completion may not clear the new request's loading state, and unmounted requests
are retired. The stale-result/error and independent loading scenarios fail
against the archived pre-repair application. Two index-only manual-refresh
cases retain the same connection while changing the subsystem index and also
fail before repair. Automatic-load success/error cancellation was already
correct; its two additional positive controls pass before and after. Generated owner schema bytes and
the locked SDK boundary are used; this does not establish browser serial delivery.

## Current test ownership and compatibility

This owner supports DYA custom Studio RPC. Stock ZMK is NOT APPLICABLE;
no feature-disabled stock check is required or counted as compatibility.

Canonical entries (run from the repository root):

```bash
python3 -m unittest discover -v -s tests -p test_owner.py
bash tests/run-web.sh
```

[maintenance.json](maintenance.json) records supported cores, current commands
and explicit gaps. It is a reference map, not a passing test or a claim that
registration/coverage totals establish completeness. Preserve ordered outputs,
raw timestamps and the first failure. Read actual dependencies before execution.

`OWNER_TEST_OUTPUT=/absolute/new/directory` retains owner native/build outputs.
An existing directory fails before tests. Without it a fresh temporary directory
is printed and retained. No shared build directory is deleted. Use
`PYTHONPATH=tests python3 -m unittest -v test_owner.WestCommandsTests.<method>`
for a focused case; discovery runs the complete documented class.

The native verifier runs each built binary three times in independent empty
initial-flash directories, retaining raw output before filtering. It checks real
process exits, fatal assertions and every linked checked-init result before
comparing unchanged owner snapshots. A SYS_INIT error cannot be hidden by a
later matching key trace. Native scheduling and controlled APIs still do not
establish physical transport/retention. Test-only core/tool dependencies are
pinned; update pins only after reviewing the new sources and rerunning this
owner matrix. No network updates are implicit in the owner test command.

## Serialized protocol boundary

```bash
python3 tests/protocol/run.py
```

This compiles the complete production handler with the owner-generated schema
and real nanopb under sanitizers and optimization. Set `NANOPB_SOURCE` to the
explicitly inspected nanopb checkout (physical-layout also needs `ZEPHYR_BASE`).
It checks malformed/unsupported/valid dispatch and recovery, exact response
fields and presence, repeated callback order, two-pass encoded size and failure
at every insufficient output capacity. Framework APIs are controlled; no core
source is patched. This complements registration/native and board checks. The profile-count regression
uses distinct standalone, one-peer central, two-peer central, peripheral and
USB-only cases: the public field reports selectable host profiles, excluding
bond slots reserved by a BLE split central. The host fixture controls the core
profile-count API. Two additional header-backed roles read the selected core's
public profile-count macro block from `ZMK_ROOT/app/include/zmk/ble.h` (or the
resolved west project), record its SHA-256 and compile it with the complete
production handler/codec. The standalone uses five paired slots; the central
uses six slots with one reserved split peer. Both must decode five host
profiles. Prepare `ZMK_ROOT` explicitly when running outside a west workspace;
the test does not fetch or change the core. Full header/build compatibility
remains in the firmware matrix.
A long-versions role checks the dirty flags against the full build strings: a
dirty version longer than its 80-byte field stays dirty although the reported
copy is cut, and a tag that merely contains "dirty" is clean. The build-id
parser reads exact-size copies of crafted notes: a SHA1 note, a padded name, a
cut descriptor, a wrong note type, a name size that would wrap a 32-bit bound
and an id longer than its field. POSIX has no linker note; the board build checks
that the boundary symbols bracket the note and that the UF2 image flashes those
exact bytes there. The runner is also `test_owner.WestCommandsTests.test_protocol`.

The retained `tests/review/test_profile_count.py` is a legacy executable that
expects an obsolete two-argument/int-return handler signature. It is unsupported
against the current handler and is not counted as passing or as a canonical
entry. Its unique assertions are mapped into the current full-source protocol
fixture; the original file remains available for evidence review:

| Legacy assertion | Current executable owner assertion |
|---|---|
| Handler successfully returns device info | `device_info_rpc_handle_request` success and the decoded response discriminator in `tests/protocol/harness.c`. |
| Build/hardware/config/runtime presence | `test_requests_and_wire`, plus `test_selected_core_profiles` decoded `has_*` assertions. |
| Selected core computes five standalone profiles | `header-standalone` reads the selected public macro definitions with five paired slots, then decodes count 5. |
| A central with six slots/one peer still has five host profiles | `header-central` uses the same selected definitions and explicitly checks `ZMK_BLE_PROFILE_COUNT == 5` and decoded count 5. |
| Exact synthetic uptime 12345 survives the codec | `test_selected_core_profiles` sets 12345 and requires decoded 12345; the existing uint32-wrap-to-9 assertion is retained separately. |
| ASan/UBSan execution | Both added header roles use the existing sanitizer mode, and full mode retains optimized variants as well. |

`tests/readiness/run.py --output NEW_OUTPUT` adds a real Zephyr deferred device
and invokes the actual core/custom handler and complete response codec. It
initializes that device after the enclosing sizing pass, then requires successful
encoding and decoded device count/order/truncated names/ready values before,
during and after initialization, with unchanged total lengths. The original
Studio key trace is retained, with a checked completion and three identical
binary runs. The canonical owner entry includes this case. The nanopb readiness
field has explicit presence and is set even for `false`; public protobuf fields
and TypeScript semantics are unchanged. It is an output-time observation, not
an atomic snapshot of all device states or a physical driver acceptance test.

For the isolated DI-1 control, run `python3 tests/review_negative_control.py
"$ISOLATED_OWNER"` from the unchanged owner. It restores both the proto3 scalar
omission and the matching handler assignment, avoiding an artificial compile
failure. Then run the unchanged readiness fixture:

```sh
python3 tests/readiness/run.py --output "$EVIDENCE/readiness-negative" \
  --module-root "$ISOLATED_OWNER" --expect-encoding-failure
```

This explicit control mode preserves all positive assertions and builds once.
Even if west's initial execution fails, it executes the resulting identical
binary three times in separate empty directories and requires the specific
`deferred-init-during-output encoded=0` observation and its encoding assertion
each time. Compile/setup failures or unrelated crashes do not establish the
control. The default positive owner entry still requires successful core/custom
decoding, unchanged Studio traces and its existing three-repeat verifier.

## Reproducible isolated checks

Use this owner's `west/west-test-isolated.yml` and its pinned test dependency
manifest. Test ZMK is `e5c9b6915b56801193e359dd9bad4a167ce0d1b8`;
Zephyr is `10ba6d0cb38bc3d258775d27982f707599320085`. Resolve the
manifest and verify actual checkout identities before running. Do not inherit
global CMake module overrides or moving production dependency references into
these isolated fixtures. The CI SDK image is pinned by digest in
`.github/workflows/zmk-module.yml`; production dependency manifests are separate.

Use a new `OWNER_TEST_OUTPUT` directory for every complete owner run. Native
repeats reuse one compiled binary with independent initial-flash state; keep
the first failure and raw combined stdout/stderr order. DYA-only compatibility
still means no stock-ZMK test requirement.

The test/tool static CI job installs pre-commit 4.7.0 and uses the pinned
Ruff/clang-format/formatting hooks. Its SKIP list avoids repeating the firmware
and Web jobs. Local full pre-commit execution may run those functional hooks;
prepare Web dependencies with the normal package lifecycle first where present.
A successful hook exit must also leave source inputs unchanged. Snapshot and
event-pattern bytes are excluded from whitespace rewriting.

## Canonical Web test location

The existing seven UI tests and their setup/Jest configuration live in
`tests/web/`. Application code and installed dependencies remain in `web/`.
`bash tests/run-web.sh` dispatches the package test command, which generates the
real Device Info TypeScript schema before Jest. Watch and coverage use the same
configuration. `npm run lint` in `web/` also checks the owner tests; the Web CI
path selection includes `tests/web/`. No assertion or snapshot was removed in
the relocation from `web/test/`.

Use `npm ci` explicitly to prepare the checked-in lockfile dependencies when
needed; no install or dependency update is implicit in the test entry. Web tests
use controlled transports and do not establish actual Serial/BLE delivery or
HWINFO behavior. Run `npm run build` and `npm run lint` from `web/` alongside the
Web tests for configuration/infrastructure changes, and retain the complete
owner firmware matrix above. Physical acceptance remains separate.

The owner command boundary limits `zmk-build` to one target at a time by
default. `OWNER_TEST_BUILD_PARALLELISM` selects an explicit positive target
count; account for compiler jobs and concurrent owners in the global budget.
`tests/test_build_budget.py` checks default/explicit forwarding and rejects
invalid budgets before launching west. The canonical owner entry includes
these controls without dropping any native or board scenario.

Native result admission requires every `native_sim.keymap` fixture to have a
matching binary. `required_hooks.json` names the expected checked initialization
hooks and required feature/role configuration. Fixture-bound declarations also
reject missing configuration when both the request and build omit it; excluded
roles cannot replace the requested role. The verifier retains all three raw
runs before comparing unchanged ordered snapshots and rejects logged failures,
fatal output and missing/nonzero completions. `tests/test_native_verifier.py`
owns eleven real-ELF admission controls, including a deliberately missing
native scenario, wrong-role and unrelated-fixture controls.

`bash tests/run-web.sh` is the canonical schema-generation/Web test entry.
It runs all configured Jest scenarios in one worker process. The pre-commit
Jest hook dispatches this same owner entry so nested workers do not multiply
the global container budget. Build and lint retain their separate package entries.
