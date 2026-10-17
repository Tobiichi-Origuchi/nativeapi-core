# HarmonyOS native integration smoke test

A small Stage-model HAP that loads nativeapi from C++, runs assertions on its
ArkTS UI thread, and shows the result. Build with DevEco Studio 26.0 / SDK API 26.
The implementation itself requires an API 23+ NDK/runtime for Preferences.

The HAP carries ARM64 and x86_64 native libraries. Run in a **disposable local
emulator** on Windows:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\run.ps1 `
  -DevEcoRoot 'E:\Program Files\Huawei\DevEco Studio'
```

On an Apple Silicon Mac:

```bash
./run.sh /Applications/DevEco-Studio.app/Contents
# After the first run, repeat without rebuilding to check process persistence:
./run.sh /Applications/DevEco-Studio.app/Contents 127.0.0.1:5555 --skip-build
```

The dialog interaction checks require Python 3 and exactly one running emulator,
with an isolated name beginning `NativeAPI_`. The runner automatically drives only
that guest, checking native bundle/window ownership before every click. No host
mouse or keyboard input is sent.

Start the emulator first. The runner accepts only a loopback hdc target whose
reported model is `emulator`. The Windows runner accepts x86_64; the Unix runner
accepts ARM64 and x86_64. It installs `dev.nativeapi.smoke`,
grants READ_PASTEBOARD to that debug HAP, launches it, and requires a fresh
`NATIVEAPI_OHOS_PASS` marker from that process. It leaves the result visible.
Logs live in `test-results/`; missing results, crashes and timeouts are failures.
Use `-SkipBuild` for a second run that also checks preferences across process restart.
Do not use test-only permission grants in a production host: request access through
the application's normal permission flow.

Wait for the emulator launcher to be ready and unlocked before running. First-boot
screen locking is reported as an explicit launch failure, not a passing test.

The runner also drives the real system document picker after the service/window
suite. To iterate only on the picker (the smaller result does not replace the full
suite), use `NATIVEAPI_TEST_SUITE=picker` with the same command; PowerShell accepts
the same environment variable. The fixture exposes a separate guarded-start button
and requires native user selection/cancellation, never simulated return values.

The independent URL suite uses `NATIVEAPI_TEST_SUITE=url` with the same runner.
It starts four fresh fixture processes: C++ and C ABI, each opening HTTP and HTTPS.
`verify_url_ui.py` requires the real SDK completion marker, browser process/window
ownership and an address field matching the unique requested URL. Phone checks use
only guarded native address/edit/back controls and, when shown, the keyboard's hide
control; they never type keys. Configure the disposable phone's input method in its
basic mode before its first address-bar test. The verifier never accepts
privacy/service consent prompts automatically.

URL tests are separate because they replace the test process and foreground the
real system browser. The default service/window/dialog/picker run retains its own
window acceptance fixture. Neither standalone suite substitutes for the complete
HarmonyOS API coverage audit.

The independent notification group uses `NATIVEAPI_TEST_SUITE=notification`. It
waits for real SDK authorization/publish/cancel results, inspects active system
records, and drives this fixture's notification action and body in the actual
notification center. Only the named isolated guest and the test HAP's authorization
prompt are operated; native ownership/PID/UID checks guard each press. Phone tests
pull down the notification center from its current native status-bar clock bounds.
No host mouse/keyboard or guest key input is used, and no other app's notifications
are cleared. The suite cleans only its known fixture tags after interrupted runs.
`NATIVEAPI_TEST_SUITE=notification-lifecycle` runs separate body/button cold-launch
cases. Each publishes a real notification, shuts down the manager and checks the
system record, then force-stops only the fixture after PID/UID verification. The
guarded notification click must launch a different PID, forward its original Want
through onCreate, deliver it once to C++ and C ABI listeners after initialization,
and remove the retained record. No launch command is used for recovery.

In the configured SDK shell, `python3 run_notification_denial.py <DevEcoRoot>`
builds a separate package with a fresh `dev.nativeapi.notificationdenied<nonce>`
identity, confirms it has no authorization, then clicks its real system refusal
button and checks the SDK error and publication guards. On computer it first
declines only that fixture's optional desktop shortcut prompt. Successful runs
remove the temporary package. The installed normal app's data stays available.
The variant replaces the output HAP, so the script invalidates the build marker;
rebuild the normal fixture afterward. Both runners inspect the actual HAP package
and device lifecycle before installation, including in skip-build mode.

Full API coverage, pending-environment teardown/concurrency and broader boundary
audits remain separate from these notification groups.

Coverage:

- Device metadata against the actual SDK, plus a generated C ABI getter.
- Application label/bundle/version/build metadata, including a worker and the C ABI.
- Native image RGBA decode, PNG/base64/file round trips, JPEG/WebP encode/decode,
  copy/move ownership, invalid encodings/paths and generated C ABI image handles.
- Real ASSET encrypted storage: Unicode/empty/binary strings, overwrite, namespace
  isolation, 1023-byte boundary, more than 40 entries, reopen, process persistence,
  removal/clear, native availability and generated C ABI paths.
- Live display enumeration, stable identity, size/density, work area and refresh rate.
- Preferences Unicode/empty values, enumeration, scope isolation, flush, reopen,
  process restart, removal, clearing and invalid keys.
- WindowStage attachment, asynchronous child creation/content loading, shared identity,
  manager/C ABI lookup, logical geometry vs the real NDK, drawable bounds,
  focusability/touchability, inactive showing, focus/blur return, hiding/restoring,
  hit-test ordering/exclusion, native geometry/focus/close events, cancellation,
  deferred/coalesced closing, retained wrapper invalidation, host-driven destruction,
  monitor restart/stop, failed/reentrant child creation cleanup and immersive
  main-window layout/full-screen state and events.
- Native ArkUI message dialogs: title/message, primary/secondary/close buttons,
  default-button configuration, prefilled input, a real checkbox click, determinate
  and indeterminate progress, live updates, reopening, programmatic dismissal,
  worker modal waiting, UI-thread modal rejection, destruction and parent closure.
  Application modality disables input on existing/newly attached sibling windows
  and restores their original policies. The guest UI driver confirms the final
  absence of native dialog nodes. Missing/unsafe press ownership is a failure.
- System document picker: single/multiple files, save and folders, Unicode/special
  file names, mixed extension filters, actual URI-authorized document writes/reads,
  pending-state/configuration rules, native user cancellation, C ABI URI lists and
  released handles. The SDK has no public programmatic Close operation; rejection
  is checked while the real picker remains open.
- Clipboard rejection before host initialization; deferred text/HTML reads, Unicode,
  multi-format writes, independent PixelMap image reads, sandbox-aware file URI lists
  with Unicode/escaping, all-four-format reads, clear, preservation after rejected writes, worker-thread writes,
  local change notifications on the UI thread, and last-listener cleanup.

The test overwrites the emulator's clipboard and its own sandbox preferences and
ASSET records. It creates and disposes its own windows and dialogs.
It does not inject mouse/keyboard events into the host desktop.

## Computer emulator acceptance

The same runners recognize a `2in1` guest and build a desktop-only manifest in the
Hvigor build model (`NATIVEAPI_OHOS_DESKTOP=1`); the source manifest is unchanged.
This is significant: mixed phone/desktop applications background their UIAbility
when the main window is minimized, while desktop-only applications keep it foreground
and can use the public window `restore()` operation. Mixed-device hosts must arrange
their own Ability foreground transition. A skipped build is rejected when the last
build used a different lifecycle configuration.

Desktop coverage adds native title/decorations, movement/resizing policy acknowledgement,
minimum/maximum size round trips, actual main-window movement/resizing, maximize and
recover, full-screen geometry, preservation of maximized state across full screen,
and main-window minimize/restore. The desktop deadline is 30 seconds for these
additional asynchronous transitions. A separate 90-second deadline covers guarded
native dialog interaction; missing conditions remain failures.

After the HAP reports PASS, exercise the actual guest title bar and its controls:

```bash
python3 verify_desktop_ui.py /Applications/DevEco-Studio.app/Contents
```

This requires the isolated `NativeAPI_Desktop` to be the only running emulator and
uses only the CLI's named guest input. It verifies native visible-window ownership
(bundle and window ID) before each press, derives widget positions from the guest
UI tree, and asserts actual WindowManagerService geometry after dragging, maximizing
and restoring. No host desktop mouse or keyboard input is sent. Results and native
snapshots are saved under `test-results/desktop-ui/`.

## Document picker UI evidence

`verify_picker_ui.py` is invoked automatically. On the computer guest it verifies
that the file-manager UIExtension is hosted by the test app; on the phone it checks
that the press lies inside the modal UIExtension of the test app's native window.
Both paths validate the native window owner immediately before every press.
The computer selects its two owned documents by a guarded rectangle gesture and
asserts the actual native selected rows; phone navigation uses current native IDs
and on-screen controls. No host mouse or keyboard input is injected. Existing
user files are never selected: names contain the fresh test-process ID.

The tests create small, uniquely named `NativeAPI-picker-*` documents only in the
disposable guest's Download directory. They are retained as test evidence. Picker
logs, native widget/ownership trees and selected-row snapshots are saved under
`test-results/picker-ui/`, with `picker-runtime.log` and `picker-ui.log` beside it.
The standalone picker suite uses `nativeapiSuite=picker` only in its own test Want;
the default host path still runs the complete service/window suite first.

For a direct SDK comparison, start the installed fixture with
`aa start -a EntryAbility -b dev.nativeapi.smoke --ps nativeapiSuite reference`
or `reference-folder`. These diagnostic Want values invoke ArkTS
`DocumentViewPicker` directly (multi-file count 10, or one folder) and render/log
the exact returned URIs. They also log the public folder system-capability query.
They do not exercise nativeapi and cannot replace its native acceptance checks.

The independent native cancellation scenario uses `nativeapiSuite=picker-cancel`
and `verify_picker_ui.py ... --pid <fresh-pid> --cancel-only`. Its distinct
`NATIVEAPI_PICKER_CANCEL_PASS` and `NATIVEAPI_PICKER_CANCEL_UI_PASS` markers verify
only C++/C ABI user cancellation and lifecycle. They never satisfy the full runner's
`NATIVEAPI_PICKER_PASS` requirement.

## Native view acceptance

Run `NATIVEAPI_TEST_SUITE=views ./run.sh <DevEcoRoot>` (or the same environment
variable with `run.ps1`) against the unlocked isolated guest. This independently
launches `pages/Views`, mounts a real NodeContent and runs native C++/C ABI checks.
The verifier confirms native PID/UID/window ownership before each guest click;
it uses no keyboard input. It checks native label/input/image presence, password
masking, hidden/disabled controls, native layout dimensions, row flex/spacing,
focus, identity after reparenting and input-node replacement, exact click delivery,
detached ownership, C handle release and image clearing. Mount/unmount/reattach
preserves root identity. Results and the initial screenshot are stored in
`test-results/view-ui/<pid>/`; this suite does not replace the full API audit.

## Native menu acceptance

Run `NATIVEAPI_TEST_SUITE=menus ./run.sh <DevEcoRoot>` (or the same variable with
`run.ps1`) in an unlocked isolated guest. The fixture uses the canonical library
ArkTS adapter copied by Hvigor, not a test substitute. It checks real native menu
nodes, selected/disabled/mixed UI states, absolute and relative anchor geometry,
dynamic label/removal, nested item selection, explicit submenu closure, C ABI
presentation/events, pending cancellation, programmatic closure and destruction.
Native PID/UID/window ownership is checked before every guest press. No host
input or keyboard injection is used. Per-PID evidence and the initial screenshot
are stored under `test-results/menu-ui/`. Actual keyboard, hover/tooltip and
remaining placement/teardown cases remain outside this fixture's current coverage.

## Accessibility state

Run `NATIVEAPI_TEST_SUITE=accessibility ./run.sh /Applications/DevEco-Studio.app/Contents`
against the selected disposable phone/computer guest. This group does not inject
input or grant permissions. It compares C++/C ABI UI and worker queries, including
pre-initialization and repeated Enable, with independent ArkTS SDK queries and the
actual AccessibilityManagerService state. Evidence is saved under
`test-results/accessibility/<pid>/<unique-run-id>/`; the run ID prevents old PASS
records from satisfying a new request. Running `run_accessibility_suite.py` again
rechecks the same live process through a new Ability Want and fresh SDK queries.
The tested disabled state does not prove enabled-state transitions; keep that
scenario open until a real extension has been activated through the system.

## Application lifecycle, quit and appearance

Run `NATIVEAPI_TEST_SUITE=application ./run.sh /Applications/DevEco-Studio.app/Contents`
with the chosen named guest running and unlocked. The test Python requires Pillow;
set `NATIVEAPI_TEST_PYTHON` to a virtual-environment interpreter when needed. The
suite starts fresh processes for approved and required termination. It checks real
SDK lifecycle forwarding, C++/C ABI cancellation/deferred worker votes, real native
window removal, primary-window identities and actual rendered Light/Dark/System
pixels against independently resolved SDK resources. Phone acceptance also checks
actual foreground/background transitions against native visible windows.
No host input or guest keyboard input is injected. Each button press verifies fresh
process/UID/native-window ownership. Evidence is under `test-results/application-ui/`
and `test-results/application-suite/`. Older failed bring-up runs are not acceptance.

### Tray acceptance

Run `NATIVEAPI_TEST_SUITE=tray NATIVEAPI_TEST_PYTHON=<python-with-Pillow> ./run.sh <DevEco>`
against the selected disposable NativeAPI_Desktop or NativeAPI_Phone guest. The
computer suite uses real system status items, SDK completions, guarded guest mouse
input, native menu trees and captured icon pixels. It verifies C++/C ABI click
delivery, image/template/tooltip updates, SDK rejection, one-item capacity, live
menus, hide/show and last-wrapper destruction. Phone runs the metadata/identity
and unavailable-service assertions. `nativeapiSuite=tray-reference` selects the
independent direct ArkTS comparison page. Results are under `test-results/tray-*`.

### Application visibility, mission icon and badge acceptance

Run `NATIVEAPI_TEST_SUITE=integrations NATIVEAPI_TEST_PYTHON=<python-with-Pillow> ./run.sh <DevEco>`
with only the selected disposable NativeAPI guest running and unlocked. Desktop
acceptance verifies real SDK whole-Ability hiding of a main/child window group,
C++/C ABI events, worker completion, stable window identities/rectangles, restoring
through Application, activating Window::Show, external launcher activation and an
actual system status-item click. It checks rendered mission-window icon pixels
and numeric system badges (9, 99+, clear) against independent SDK badge reads.
Phone acceptance compares unsupported desktop operations with the direct public
SDK and checks badge state and launcher rendering. Input guards verify guest,
fixture PID/UID and native window ownership; no host input or keyboard is used.
Evidence is under `test-results/app-integration-{suite,ui}/`. Desktop system hiding
removes the running Dock item on this image; external launcher activation is a
separate tested restore path, not a claimed Dock click.

### Window occlusion acceptance

Run `NATIVEAPI_TEST_SUITE=occlusion NATIVEAPI_TEST_PYTHON=<python-with-Pillow> ./run.sh <DevEco>`
on either selected disposable NativeAPI guest. Two real child windows provide
uncovered, partially/fully covered, transparent, hidden and restored scenes.
A separate public ArkTS occlusionStateChanged listener supplies the SDK reference;
C++ and C ABI queries run on UI and workers. Object/manager/C events must agree,
aggregate Visible changes must not duplicate on partial/no-occlusion transitions,
and queries must remain current while all public observers are removed. Retained
closed wrappers return Unknown. System window order/rectangles and actual magenta,
cyan and blended pixels verify the rendered scenes; guarded Next presses advance
only after verification. Child shadow radius is explicitly zero for full-coverage
scenes because SDK occlusion includes shadow extents. Native SDK observers remain
active for queries even without nativeapi listeners. Results and failures are kept
under `test-results/occlusion-suite/`; no host cursor or keyboard input is used.

Phone badge preview also verifies that the fixture's actual launcher icon is on
screen. If backgrounding reveals a previous app (for example Settings), the
verifier first restores its own fixture window and performs a Home gesture from
that window's fresh verified bottom edge. It checks the fixture PID/UID and native
owner before touch input, then verifies the actual launcher icon afterwards. It
never sends touch or keyboard input into the previous application.

### Window corner and shadow acceptance

Run `NATIVEAPI_TEST_SUITE=window-style NATIVEAPI_TEST_PYTHON=<python-with-Pillow> ./run.sh <DevEco>`
with one selected disposable NativeAPI guest running and unlocked. The suite
compares real SDK corner radii and captured compositor mask pixels for square,
small/normal rounded and restored original corners. C++/C aliases, UI/worker calls,
invalid/dead windows, overlapping requests and direct SDK changes are covered.
Shadow on/off images must match independent direct SDK calls, with actual exterior
pixel darkening and stable native geometry. The fixture declares the normal
system_grant SET_WINDOW_TRANSPARENT permission to test main-window shadows on
supported desktops, in addition to permission-free child shadow radius calls.
Each scene advances only after guarded Next input and system/pixel verification.
No host mouse or keyboard input is sent. Results are under `test-results/window-style-suite/`.

## Native window polygon masks

With one unlocked `NativeAPI_Desktop` or `NativeAPI_Phone` guest, run:

```sh
NATIVEAPI_TEST_SUITE=window-shape NATIVEAPI_TEST_PYTHON=/path/to/python-with-pillow \
  ./run.sh /Applications/DevEco-Studio.app/Contents
```

The suite compares actual compositor pixels with an independent geometric oracle
and verifies real touch delivery in both opaque and transparent regions. It covers
triangles, a concave polygon, even-odd self-intersections, off-window polygons,
rectangle clearing, immediate SDK resize, SDK reference masks, C++/C aliases,
UI/worker calls, pending overlap and closed wrappers. Every touch is guarded by
fresh native guest window/process/UID ownership. It never sends host input.
`test-results/window-shape-suite/` retains native logs, layout/WMS dumps, PNGs,
pixel samples and touch-owner evidence, including failure evidence.

## Native window dragging and restricted input-area audit

Run `NATIVEAPI_TEST_SUITE=window-drag` with the same Pillow-enabled Python and
one unlocked named guest. The suite calls C++/C StartDragging synchronously from
the owning ArkTS onTouch-down callback, compares direct SDK behavior, and checks
real WindowManagerService movement, release and click-only geometry. Desktop
also exercises native mouse drags. Positive scenes require actual move events;
object/manager/C subscriptions must agree on counts and window identity.

Phone child windows move; the full-screen phone main window must stay put and
the direct SDK reference must reject it. The fixture also records normal APL,
non-system application identity and the actual SET_WINDOW_TOUCH_AREAS denial for
valid rectangles in both main and child windows. It never grants that restricted
permission or changes the app privilege level. Restricted-ACL approval scenarios
remain a separate open audit. All input is confined to the disposable guest.

## Native window control buttons

Run `NATIVEAPI_TEST_SUITE=window-controls` with the same Pillow-enabled Python
and one unlocked named guest. Eighteen scenes verify C++/C visibility, exact
object/manager/C property-event counts and actual native title-button nodes.
They cover repeated setters, worker/UI requests, direct SDK changes, individual
button subsets, hidden title bars and explicit overrides, unlisten/relisten,
unsupported children and closed wrappers. Computer scenes also maximize and
recover programmatically while all buttons are hidden. Phone requests must leave
the controls absent without reporting successful visibility changes.

Results are under `test-results/window-controls-suite/`. Each press checks fresh
guest process, UID and native window ownership; no host input is used.
Run the fixture alone: a concurrent Emulator layout analysis can compete for
the guest UITest service and invalidate a scene's observation.

`run_control_probe.py` is an independent SDK investigation, invoked with the
DevEco directory and optional `--device` after installing the HAP. It records
fifteen title-button/window-mode scenes, raw SDK error codes, native trees,
geometry and screenshots. Its observations do not assert nativeapi support for
complete capability policies. The caption-only implementation and its explicit
native double-click limitation are covered separately below.

## Independent caption policies

Run `NATIVEAPI_TEST_SUITE=window-policies` with the same Pillow-enabled Python
and one unlocked named guest. Thirty-two scenes verify C++/C independent
minimize/maximize/close button choices, worker/UI calls, repeated requests,
object/manager/C event counts, collective hiding, title-bar changes, direct SDK
edits, actual programmatic minimize/maximize/recovery and child destruction.
Normal non-system metadata is required. Unsupported phone/ordinary-child SDK
requests must preserve false policy getters and zero events.

The computer fixture checks exact native button identities, performs one real
native maximize-button click and two guarded title double-clicks. The latter
explicitly characterize an SDK limitation: the native title gesture still
maximizes while the maximize caption button is hidden. This suite does not claim
complete user-maximize denial. Recovery after that gesture keeps the subsequent
programmatic maximize test independent. Minimized UIAbility foregrounding is
performed through its normal Want lifecycle before calling Restore.

Every input checks the named guest, PID/UID and fresh native window ownership.
No host input or keyboard keys are injected. Results, scene JSON, native trees,
WMS records, decoded screenshots and direct SDK reference errors are retained
under `test-results/window-policies-suite/`.

## Fullscreen windows and independent SDK mode probe

`NATIVEAPI_TEST_SUITE=window-mode-policy ./run.sh <DevEco-Contents>` verifies
supported-mode policy through C++/C UI/workers on a real primary, eligible child
and ordinary child. The 43 scenes compare native policy, real SDK calls (a
delegating trace preserves the actual SDK outcome), independent SDK transitions,
WMS geometry and object/manager/C property events. They cover queued cross-alias
toggles, absent-Window-method Stage fallback, host masks with SPLIT disabled,
ordinary-child fullscreen enablement, FULL_SCREEN-only empty-mask refusal,
invalid baselines and real child destruction with four pending requests. A test
adapter delays only the real SDK outcome; after destruction, a waiting worker
must finish and releasing the late outcome must not publish state or events.
The runner sends no synthetic input.

`NATIVEAPI_TEST_SUITE=window-fullscreen ./run.sh <DevEco-Contents>` exercises
actual main/eligible-child fullscreen and recovery through C++/C UI/workers.
The 31 scenes compare public SDK status/properties, native logical geometry,
independently parsed WMS frames and object/manager/C enter/exit events. They
include repeated requests, restoring a previously maximized child, direct SDK
changes, ordinary-child refusal, SDK-supported-mode denial/restoration and
actually destroyed/invalid/released wrappers. The runner uses normal Wants to
acknowledge scenes and sends no synthetic input.

`window-mode-probe` is a separate twenty-eight-scene public SDK reference. It records
native fullscreen/maximize mode restrictions and restoration on main/child
windows, plus ordinary-app mouse-monitor permission results and current C++/C
global-session refusal. It is independent capability evidence, not nativeapi
fullscreen acceptance. Neither probe requests or shell-grants extra permissions.
It also independently verifies phone child/ordinary content-layout flags and
unchanged floating frames, separating those from actual freeform fullscreen modes.
The added mode-mask scenes record the declaring AbilityInfo after actual SDK
overrides and independently upgrade an ordinary child's allowed native modes.

## Custom title-bar actions

Run `NATIVEAPI_TEST_SUITE=window-title` on one unlocked named guest. The normal
HAP forwards real ArkUI double-tap/double-click events from its own main and child
bars to PerformTitleBarDoubleClick through C++/C on UI/workers. Thirty-one scenes
and twenty guarded guest double-clicks verify actual SDK/WMS rectangles,
floating/maximized transitions, real native-menu split-screen recovery, disabled
policies, fullscreen/minimized rejection,
three shared pending-request guards, ordinary-child SDK rejection, closed/foreign/
released handles and matching object/manager/C resize events. The computer host
creates an eligible child with `maximizeSupported: true`; its caption button
setter still returns 1300004 and is not used as an action-capability test.

New children are shown and measured before their floating reference rectangle is
captured, then minimized while exercising the main window. Ordinary-child
refusal is tested after showing it in actual floating mode, rather than masking
the unsupported type behind a minimized-state rejection. A never-shown child's
initial zero rectangle is not a valid recovery oracle. The minimized main scene
calls the API directly on UI/workers; a normal Want foregrounds the Ability before
Restore. Phone gestures run against the same custom bars and qualify unsupported
state/type paths rather than successful computer maximization.

`window-title-probe` separately records seven direct public-SDK child scenes
(maximize/recover, title buttons, decorations and destruction). It sends no input
and makes no nativeapi acceptance claim. Both suites retain normal metadata,
scoped logs, native trees, WMS frames and decoded screenshots under their matching
`test-results/*-suite/` directories. Every double-click checks fresh guest process,
UID and own-window ownership; no host input or keyboard keys are sent.

## Content aspect constraints

Run `NATIVEAPI_TEST_SUITE=window-aspect` with the same Pillow-enabled Python and
one unlocked named guest. Sixteen scenes compare successful C++/borrowed/C
constraint state with actual native edge resizing, independent SDK constraints
and WMS geometry. Computer scenes use guarded atomic mouse drags inside the
straight right border, away from transparent corners. The suite checks square,
wide and fractional ratios, removal, SDK refusal, nonfinite input, ordered UI
requests, worker completion, programmatic resizing, hidden/normal title bars,
size-limit intersection, child refusal and destruction with queued requests.

The backend requests `setContentAspectRatio(ratio, false, false)`: it does not
change the current rectangle or persist a constraint to later windows. The SDK
does not expose a ratio getter, so nativeapi reports its own last successful
constraint and documents that direct host changes are not reflected in that
getter. Real SDK/compositor behavior is checked separately. Evidence, including
native trees, identity guards, WMS rectangles and decoded screenshots, is saved
under `test-results/window-aspect-suite/`. Do not run other guest UITest commands
concurrently with the fixture.

## Phone picker SDK comparison

The test page accepts `nativeapiSuite=reference` or `reference-folder` to call
`DocumentViewPicker` directly, independently of nativeapi. Optional string Want
parameters are `nativeapiPickerStartUri` (the SDK's defaultFilePathUri),
`nativeapiPickerFilter=none`, and `nativeapiPickerMerge=document`. Folder mode
omits file filters. The page records the actual options, SDK result and structured
error in `SDK_REFERENCE_*` logs. `ReferenceGuest` in `verify_picker_ui.py` checks
those real pending markers while retaining native process/window/modal ownership
checks. These paths are diagnostics, not passing nativeapi acceptance tests.

For comparison with the separately installed release HarmonyOS 6.1.1/API 24 phone
image, build with `-p product=release24` instead of `product=default`. Its HAP is
under `entry/build/release24/outputs/default/`. This explicit test variant keeps
target API 26 and lowers only the declared minimum version; it does not establish
library-wide API 24 compatibility. The default runner still builds/installs only
the normal API 26 product. Verify the packed manifest and actual library loading
before interpreting results. `NATIVEAPI_OHOS_INSTANCE=NativeAPI_Phone24` enables
that exact disposable instance in `Fixture`; its default instance checks remain
unchanged. Initial scenario CLI requests could not connect to the release
guest; the comparison used owner-guarded guest UITest coordinates derived from
fresh trees. This observation does not establish a CLI compatibility limit.

## Cursor query comparison

After installing the normal HAP, run the real guest-only comparison with an
explicit hardware expectation:

```sh
python3 run_cursor_suite.py "$DEVECO_STUDIO_HOME" --expect-pointer absent
```

`absent` requires the public SDK's exact `INPUT_DEVICE_NO_POINTER` result in the
foreground, plus matching safe defaults in UI/worker C++ and C ABI calls. It emits
`NATIVEAPI_CURSOR_ABSENT_PASS`; it cannot satisfy the `present` scenario. Use
`--expect-pointer present` only with an actual pointer available: it requires
three distinct nonzero positions, agreement with delivered ArkUI click coordinates
and physical press points, and SDK/UI/worker/C agreement. Both scenarios compare
actual window focus loss and restoration. With no pointer, that SDK error takes
precedence even after focus loss; this does not prove the authorization-denial
branch. The `present` scenario separately requires INPUT_APP_NOT_FOCUSED there.
No input-device permission is granted.
Results retain SDK codes, native window/UID ownership, real event coordinates and
screenshots under `test-results/cursor-suite/`. Multiple displays and the positive
hardware case must remain open until exercised on a suitable environment.

### Live display notifications

Run the following on one unlocked disposable named guest:

```sh
NATIVEAPI_TEST_SUITE=displays NATIVEAPI_TEST_PYTHON=/path/to/python-with-pillow \
  ./run.sh <DevEcoRoot>
```

The runner selects
phone `rotation` or computer `fullscreen` from the actual guest device type.
PowerShell accepts the same environment variables. Set `NATIVEAPI_DISPLAY_MODE`
explicitly to select a mode; `quiet` covers registration/constant state only and
has its own result marker. A quiet pass does not prove event delivery.

The phone scenario requires actual SDK orientation/pixel-size changes. The
computer scenario requires real full-screen/recover transitions and a changed
SDK available area. Both compare four nativeapi C++/typed/C/async-C subscription
paths against independently registered public NDK observers. Each scenario
checks held object/C-handle identity, live getters versus SDK snapshots, partial
and final unsubscription, worker subscription changes and a new observer after
queue cancellation. Commands use unique request IDs inside the verified test
Ability; no host mouse or keyboard input is used. Original geometry is restored
in a finally block.

The HAP installs one test dispatcher during module initialization, before native
subscriptions. Normally it forwards directly to the real OHOS NAPI scheduler.
Only the queued-cancellation scene holds nativeapi work in its own queue while
the UI and SDK continue running. The test requires actual SDK changes and a
positive production pending-notification count before replacing all observers
and draining the old work on UI. This proves queued-notification cancellation;
it does not inject display events or claim that blocked UI completes window
transitions. All other scenes exercise ordinary asynchronous delivery. The
fixture records native logs, SDK values, window/service state and screenshots
under `test-results/displays-suite/`.

Hotplug, multiple displays, native-ID reuse, SDK fault injection and environment
teardown require further scenarios; these tests do not claim that coverage.

### Independent clipboard interoperability

With exactly one unlocked named NativeAPI phone or computer guest:

```sh
NATIVEAPI_TEST_SUITE=clipboard NATIVEAPI_TEST_PYTHON=/path/to/python-with-pillow \
  ./run.sh <DevEcoRoot>
```

The normal runner builds the separate `../ohos_clipboard_peer` project,
then validates and installs its SDK-only HAP (`dev.nativeapi.clipboard.peer`).
`--skip-build` requires both fixture HAPs to have already been built. PowerShell
has the same branch; its runtime is not covered by the macOS-host evidence.

The peer imports public ArkTS Pasteboard/File/Image APIs and packages no native
libraries. The driver verifies distinct process identities and application UIDs.
It grants READ_PASTEBOARD only to these debug emulator fixtures and brings the
requested app forward before reading. All commands use unique Want request IDs;
there is no host pointer or keyboard input. Existing consumer and peer files
have the same logical private path but different UTF-8 contents. A positive
foreign-file result requires two real readable authorized paths, exact producer
bytes in order and no consumer `/data/storage` alias. Empty results cannot pass.

The suite checks both transfer directions, native C++ and generated C publication,
C++/C/worker reads, file-only reads, exact 3x2 RGBA pixels, text/HTML, external
change callbacks, last-listener removal and foreign clear. Eight direct FileUri
checks cover own readable files, foreign private aliases, dot segments, deleted
files, real app-UID read denial and remote URLs. Screenshots, fresh request JSON,
process metadata and per-command native/peer logs are stored under
`test-results/clipboard-suite/`. Broader URI grants/revocation, directory/file-manager
interop and teardown remain separate coverage.


### Public global hotkey registration

```sh
NATIVEAPI_TEST_SUITE=shortcuts NATIVEAPI_TEST_PYTHON=/path/to/python-with-pillow \
  ./run.sh <DevEcoRoot>
```

This registration-only scenario builds the separate SDK-only peer used by the
clipboard tests. Its new InputConsumer commands hold the same Ctrl+Alt+F9 chord
in a different process and UID. The scenario requires native registration and
unregistration, real cross-app occupancy errors, four left/right modifier-side
variants, alias/order conflict rejection, C++/C/worker paths, held identities,
metadata, scope filtering and asynchronously drained lifecycle events. Final
cleanup releases both apps' reservations. Original file/clipboard peer commands
are unchanged. Results and fresh per-command logs are in `test-results/shortcuts-suite/`.

`NATIVEAPI_SHORTCUT_REGISTRATION_PASS` deliberately requires zero activation
callbacks: no keyboard input is injected, and registration evidence is not proof
of a real press/release. The separate explicitly authorized input scenario below
now verifies actual global-key activation and disabled behavior. Application-local
scope, physical/layout variants and keyboard-monitor permission/lifecycle coverage
remain separate work. PowerShell has the same runner branch; its runtime
is not covered by the macOS-host results.


`run_shortcut_input_suite.py <DevEcoRoot> --allow-guest-keyboard` is a separate
opt-in input scenario. Use it only with explicit session authorization for guest
keyboard input: the gui-test skill otherwise forbids keyboard injection. It
accepts only NativeAPI_Phone / NativeAPI_Desktop, checks exact PID/UID and the real native focus
window before every complete Ctrl+Alt+F9 chord, and always releases the keys.
Four modifier-side variants, background operation in the independent peer,
manager/shortcut disable, callback self-removal and no delivery after removal
have stronger activation assertions. Independent direct SDK callbacks distinguish
a transport/device refusal from a backend delivery failure. This scenario has
its own `NATIVEAPI_SHORTCUT_INPUT_PASS` marker; a registration-only pass cannot
satisfy it. Do not add the opt-in flag to normal smoke/registration runs.

With the human's explicit session approval, both named guests passed nine actual
Ctrl+Alt+F9 chords: six business callbacks/activation events, nine independent SDK
callbacks and sixty native assertions each. SDK callbacks also advance while the
manager/shortcut is disabled and after self-removal, proving those negative
business results followed real input. Background delivery is observed before a
state-query Want can refocus the source. Focus dumps accompany every chord; the
desktop adds per-chord owner/key JSON. Input codes and bundles are allowlisted,
and guest identity/focus is checked again immediately before input. C-registered
callback delivery and complete teardown remain separate audits.

### Native ArkUI drop destinations

`NATIVEAPI_TEST_SUITE=drop-target ./tests/ohos_smoke/run.sh "$DEVECO_STUDIO_HOME"`
(or the same suite with `run.ps1`) builds and installs the normal fixture, mounts
NativeAPI's content root and runs `run_drop_suite.py`. The fixture uses an ArkTS
SDK source, not a fabricated native drop event. Thirteen guarded guest touch drags
cover Unicode text, mixed text/two files, files without text, leaving the target,
None/Move/Link refusal, last-listener removal, destruction from the enter callback,
and C ABI registration/data/removal. Actual file bytes and logical content
coordinates are asserted. The C ABI represents empty text as a null pointer.
No keyboard input or host-desktop input is sent.

Only Copy on the mounted native content root is implemented. Cross-application
file grants, folders, arbitrary ArkTS/Flutter content, source-side DragSource,
WindowDragSession, overlapping child destinations and environment/worker teardown
remain separate acceptance work. The named phone and computer guests are tested
sequentially; this suite does not convert a stub into a claim that the OS lacks
an API.

### Programmatic native drag sources

`NATIVEAPI_TEST_SUITE=drag-source ./tests/ohos_smoke/run.sh "$DEVECO_STUDIO_HOME"`
(or the equivalent PowerShell selector) drives NativeAPI sources into an
independent ArkTS SDK destination. No native content root is mounted: an ephemeral
scoped native node supplies the UI instance without replacing the ArkTS page.
The phone suite uses 25 actual touch drags; the computer adds C++/C mouse drags
for 27 gestures. They cover text, mixed and file-only payloads with exact file
bytes, custom preview pixels retained after property/handle changes, refusal,
leave/cancel, active-session conflicts, invalid paths/NUL/no-data/operations,
main/worker release while dragging, destruction from completion and C ABI state.

A direct SDK reference action verifies that source completion has no coordinates
and arrives on an SDK worker. `DragSource::IsPositionSupported()` and its generated
bindings report this limitation; the event position is an unavailable origin
fallback. The SDK StartDrag return code alone is insufficient: immediate ENDED
without STARTED is treated as refusal. Library completion uses guarded UI
callbacks and retains immutable SDK data/preview until the OS drag ends even
when the public source was released. Environment cleanup is guarded against
concurrent source destruction; real environment teardown remains a separate audit.
The SDK has no source-offer mask: IsOperationSelectionSupported() is false and
explicit Move/Link source preferences cannot start. The receiver can still choose
Move; the source reports that real outcome instead of disguising it as cancellation.
Other-app file grants, folders and multiple pointer layouts remain open. This suite injects only guarded guest touch/mouse input, never keys.

### System title-bar close requests

`NATIVEAPI_TEST_SUITE=native-close ./tests/ohos_smoke/run.sh "$DEVECO_STUDIO_HOME"`
(or the PowerShell selector) uses real SDK-created decorated children and the
system-provided close button. On the computer, eighteen owner-guarded guest clicks
exercise main/child cancellation, C vetoes, C++/C worker decisions, repeated clicks
and API requests, exceptions, listener removal during pending votes, external SDK
destruction, stale votes and no-observer SDK closing. Final main-window approval
must end the actual Ability process through the original SDK close path.

The phone's SDK-created decorated children and main window expose no system
caption controls in the tested profile. Eight programmatic child scenarios
exercise the supported Close path there; absent system buttons are recorded from
fresh native UI trees rather than replacing a native click with a fabricated
callback. Every child must disappear from WindowManagerService, and all public
aliases must invalidate. Evidence includes actual callback threads and request
counts, per-click PID/UID/window ownership, UI trees and screenshots. No keyboard
or host input is sent. Host replacement of the SDK close callback, other SDK/device
versions and full environment teardown remain separate audits.

### Application-local shortcut input

After the registration suite builds/installs the fixtures, run
`python run_local_shortcut_suite.py "$DEVECO_STUDIO_HOME" --allow-guest-keyboard`
only with explicit keyboard-input authorization. It accepts only NativeAPI_Phone
and NativeAPI_Desktop, checks app PID/UID and real focus before each bounded
Ctrl+Alt+F9 chord, and releases every key in finally. It never sends host keys.
The shared guard is also used by `run_shortcut_input_suite.py`; normal run.sh and
run.ps1 registration remain free of keyboard injection.

The local scenario mounts the canonical NativeShortcutHost adapter. It covers
all four modifier-side pairings, real foreground and normal peer background input,
disabled manager/shortcut with positive component delivery, host detachment/remount,
callback self-removal, C callback/user-data lifetime, worker registration and
activation-listener removal before the business callback. A separate SDK-only app
must acquire all global variants while ours is local, and local re-registration
must succeed after that different UID already owns them. Native-only component
hosting, additional windows, held-key focus transitions, repeat timing and full
environment/concurrency audits remain separate work.

### Startup status

`NATIVEAPI_TEST_SUITE=startup ./tests/ohos_smoke/run.sh "$DEVECO_STUDIO_HOME"`
(or the PowerShell selector) compares C++/C state to independent public SDK reads
for three rounds. `run_startup_suite.py --settings-transitions` separately drives
the computer's actual Settings startup list through four add/remove changes,
owner-checking the exact NativeAPI Smoke row, Settings PID/UID and focused window.
It restores the initial state in finally; no keyboard input is sent. UI getters
are asynchronous refreshes of the last completed value; workers obtain fresh
SDK results. Phone device capability refusal is recorded explicitly. The direct
input-monitor probe must return permission denied in the normal fixture; ACL
support and real monitoring are covered separately by the keyboard checkpoint below.

### Global keyboard monitoring (separate, explicitly authorized input)

Use `build_keyboard_fixture.py "$DEVECO_STUDIO_HOME" --profile <profile>` and
`run_keyboard_monitor_suite.py "$DEVECO_STUDIO_HOME" --profile <profile> --allow-guest-keyboard`
on macbook-pro after sourcing the OHOS environment. The input flag may be used
only with human authorization for this exact Ctrl+Alt+F9 chord in the named
NativeAPI_Phone / NativeAPI_Desktop guests. Every chord verifies the only running
instance, actual focus, fixture process and UID, and releases keys in finally.
All four left/right Ctrl/Alt variants are exercised. No host input is sent.

Profiles are deliberately separate:

- `ordinary`: normal dev.nativeapi.smoke, no monitoring permission; real focused
  component input must arrive while C++/C monitors remain inactive.
- `normal-acl`: isolated dev.nativeapi.keyboard.monitor with normal APL and an
  SDK debug ACL profile. On the tested HarmonyOS 7 image, self-permission is
  granted but actual NDK monitor registration still returns 201.
- `system-acl`: the same isolated namespace, freshly installed with the official
  SDK test system-app/debug profile (system_basic). The actual NDK monitor must
  succeed before the positive scenario is allowed to send its test input.

Only the isolated keyboard namespace is recreated when changing profiles. The
signer copies SDK sample keys into ignored test output, generates a short-lived
profile for this emulator's UDID, signs and verifies the HAP; it never edits the
SDK, uses personal certificates or produces a distribution signature. The HAP
checker rejects a different package or permission list. Build outputs, signed
profiles, package/token metadata, real focus/owner captures and per-command event
journals remain under test-results. The private keyboard app has its own label.
A guarded helper dismisses only the test app's own optional desktop-icon prompt.

The positive scenario checks native event sequences, C++/C and multiple monitor
instances, background delivery to another fixture UID, repeated Start/Stop,
worker lifecycle calls, listener removal, held real SDK queues, epoch replacement,
C++ destruction, callback self-stop, C callback handle release, and destruction of
the final C++ monitor from its own event. C user-data release is checked on the
next main-loop turn, matching the common asynchronous cleanup contract.
Modifier-state snapshots, live environment replacement, permission revocation and
remaining monitor audits stay open. Global monitoring is not available to normal
apps merely because these system-app debug checks pass.

### Device information and live user device names

`build_device_info_fixture.py "$DEVECO_STUDIO_HOME" --profile writer` builds a
separate dev.nativeapi.deviceinfo.writer with only MANAGE_SETTINGS in an SDK
system-app debug profile. `--profile ordinary` builds/installs the normal reader
without that permission and keeps a stable reader HAP in test-results. Then run
`run_device_info_suite.py "$DEVECO_STUDIO_HOME"`. Both named guests are supported;
only one runs at a time. No mouse or keyboard input is sent.

The writer uses the public Settings API for two fixed test names and restores
the original. It saves that original to its own SDK Preferences before mutation;
updates preserve its data, and a restarted writer can restore an interrupted
scenario. It refuses to overwrite an unrelated external device-name change. The
normal reader compares every C++/C getter against independent ArkTS SDK reads,
external uname release and the actual loaded native library's ELF identity. Four
workers repeat all getters with UI dispatch available. Pre-host and detached
context reads must use the market-name fallback; reattachment and a fresh reader
process must see the current user name. The SDK is sampled before/after each
batch, so a changing setting cannot be mistaken for a coherent result.

A separately namespaced test target compiles the actual DeviceInfo backend with
controlled NDK/uname sources: null/empty/changed values, version fallback, string
ownership, kernel failure and a kernel machine different from the process ABI.
A private SDK-shaped module uses the real N-API engine to check thrown calls,
load failures, missing methods/modules and wrong return types, including absence
of residual JavaScript exceptions. All 39 controlled assertions run once per
reader process. Those substitutions are confined to the fixture and never enter
production or modify the application's actual Settings SDK module.
The test signer is shared with the keyboard fixture through sign_debug_fixture.py;
sign_keyboard_acl.py remains its compatible keyboard entry point. Only the two
explicit fixture namespaces/permission sets are accepted.

### Preferences interoperability, concurrent lifetime and persistence

Run on a named, disposable guest:

```bash
NATIVEAPI_TEST_SUITE=preferences NATIVEAPI_TEST_PYTHON="$HOME/Library/Huawei/test-tools/bin/python" bash run.sh "$DEVECO_STUDIO_HOME"
```

This uses only
the ordinary dev.nativeapi.smoke application and a unique bounded test scope;
no mouse/keyboard input or additional permission is required. The default scope
is queried for constructor metadata but never mutated.

Eight rounds compare exact C++/C maps against independently reopened public ArkTS
Preferences, including SDK-to-native publication. The SDK reference must use the
application context (as the NDK does), the exact byte-encoded filename, and GSKV
on these tested GSKV-capable images. A module-context store or an XML store at
the same logical name is not the same storage. Production keeps the existing
NDK engine selection; this change does not migrate or rename stored data.

Checks cover UI and worker operations, both constructors, all inherited storage
methods, Unicode/empty/delimiter strings, C string/list/map ownership, released
handles, exact scope isolation including traversal and NUL bytes, repeated missing
deletion, eight competing removals (exactly one succeeds), eight parallel writer
instances, and unanchored concurrent open/final-close/reopen. Real 1024-byte keys
and 16 MiB values roundtrip; oversized input preserves earlier values. Two actual
process restarts verify persisted data and persisted clear. Each run requires
exact per-round assertion counts and SDK maps and archives all three process
roots; failure is never accepted as a skip.

### Window manager identity, lifecycle and actual SDK stack

Use `NATIVEAPI_TEST_SUITE=window-manager` with the normal run.sh harness and the
Pillow-enabled Python configured for the remote guest. No keyboard or mouse input
is sent by this scenario. It creates two real ordinary children, overlaps them
using SDK-derived geometry, changes their z-levels without changing focus, raises
one through the SDK, and hides/restores/destroys them.

Nine phases require a successful independent getWindowsByCoordinate reference,
exact C++/C worker hit/exclusion results and fresh UI results after completion.
The initial UI query is recorded separately: it uses the last completed stack
and submits a refresh, so it can differ after an external SDK raise. At least two
real differences followed by matching refreshed snapshots must be observed.
Four native workers take eight coherent samples per phase. Enumeration includes
hidden live windows, preserves canonical IDs, owns its C handles and survives
caller snapshot edits. Destruction removes windows from lookup and enumeration.
The exact per-phase count is checked, including invalid IDs/nonfinite points.

Production requests are grouped by each attached window's SDK display ID. UI
requests coalesce per display; workers always submit after invocation rather than
reusing an older pending UI request. Completed sequence and environment identity
guards protect the caches; SDK refusal retains the prior completed order or the
initial observed-order fallback. Multiple-display runtime, foreign coverage,
SDK faults/teardown and mask/clipping audits remain open. Legacy WindowManager
swizzle hooks are not changed or counted as completed by this scenario.

The device-info/preferences export array uses its actual length, preventing an
added descriptor from hiding another test entry. Both existing full metadata and
preferences suites are rerun against the same installed ordinary HAP.


### Live window parent changes

Use `NATIVEAPI_TEST_SUITE=window-parent` with run.sh and a Pillow-enabled Python.
The ordinary fixture creates three real child windows. Seventeen scenes compare
C++/C UI and worker reads with independent public getParentWindow results. Changes
between main/child parents, direct SDK changes, same-parent SDK refusal, cycle
rejection, pending-request rejection/retry, invalid/foreign/wrong-type handles,
and actual old/active parent destruction are checked. The runner requires 420
native assertions and zero failures. No synthetic mouse or keyboard input is used.

Public SDK setParentWindow is API 19+ and depends on device/freeform support.
An unavailable setter is compared with the direct SDK, rather than treated as a
successful change. UI results report submission; workers wait for the actual
promise. SDK refusal preserves the real relationship. nullptr detachment remains
unsupported; the getter returns only attached canonical parents. Native destruction
and environment cleanup release pending workers. Multiple environments, service
failure, API 19/older images, modal focus transfer and teardown races remain open.


### Real mouse hover tooltips

Use `NATIVEAPI_TEST_SUITE=tooltip` with run.sh and Pillow-enabled Python. The
fixture supplies the canonical NativeTooltipHost to its real ContentSlot, and
uses public guest-only uinput mouse movement in NativeAPI_Phone/NativeAPI_Desktop.
Each movement derives its target from a fresh native tree and verifies actual
window PID/UID ownership. No host mouse or keyboard input is used.

Twenty-four real popup scenes require exact native text/geometry, native window
focus preservation, underlying control clicks, Unicode/C tooltip updates and
clearing, disabled/hidden ancestors, removal/readdition, native TextInput to
TextArea replacement, ContentSlot detach/reattach, host replacement and destruction.
The fixture records real hover-request age: pending cancellations must occur
before the 700 ms display deadline, then remain absent after that deadline.
Window-control actions arrive through explicit test-only Wants, so changing text
while hovering does not move the mouse away from the actual native target.

Normal native-view tests continue without a tooltip companion, verifying the
optional adapter does not change existing host requirements. SDK failures, old
images, multiple windows/environments and complete theme/placement audits remain
open; missing NDK tooltip attributes alone do not mean display is unsupported.

## Foundation values and window positioning

`NATIVEAPI_TEST_SUITE=foundation ./run.sh <DevEco-Contents>` runs the shared
value/ABI contract suite on both the real ArkTS UI thread and an independent
N-API worker. It covers colors, geometry/insets, placement enums, keyboard
accelerator values and every modifier mask, polygon/shadow configuration,
positioning strategies and event request decisions. It sends no keyboard input.

The fixture also creates a real SDK child, compares C++/C positioning against
independent `drawableRect`/`windowRect` snapshots after two SDK moves/resizes,
and verifies safe geometry invalidation after SDK destruction. An isolated
wrapper separately proves final shared/C-handle release independently of SDK
promise garbage collection. Configuration tests do not establish native shadow
rendering, cursor availability or accelerator delivery. The host CTest target
`foundation_values_test` runs the same pure suite without opening a window.

## Custom window shadows

`NATIVEAPI_TEST_SUITE=window-shadow ./run.sh <DevEco-Contents>` runs the public
C++/C custom-shadow API against an actual child on UI and workers, verifies copied
configuration and independent getters, and compares real pixels, SDK helper
geometry and guarded input through 21 scenes. It covers deferred visibility
switches, native moves/resizes/hide/show, triangle/off-window masks, default
restoration, helper reconstruction and SDK destruction. The companion comes from canonical library
`ets/NativeWindowShadowHost.ets`; its private surface is never a nativeapi Window.

The runner independently counts the three-box horizontal blur at an interior
scanline, checks hard RGB/alpha over white, and verifies each touch using fresh
SDK identities/rectangles, own PID/UID, UITest roots and WMS visibility. Only known
own SDK windows may use UITest `origBounds` when the phone clips the main root;
visible foreign roots still reject the action. It never sends keyboard input.

`NATIVEAPI_SHADOW_GEOMETRY_ONLY=1` is an explicitly partial diagnostic: it runs
state/geometry scenarios without pixels or input and records zero touch probes.
It is excluded from full acceptance and archive checks. The separate
`window-shadow-probe` suite is a direct-SDK helper feasibility reference; it does
not claim the native API has been validated.

`NATIVEAPI_TEST_SUITE=window-shadow-main` runs the primary-window extension:
computer color/alpha pixels over the captured desktop, real geometry, input,
decorated refusal/suspension, helper reconstruction and default SDK restoration;
phone independently verifies SDK 801 and unchanged configuration after refused
C++/C/UI/worker requests. It sends guarded clicks only inside the test app.

### Immersive content layout

Run `NATIVEAPI_TEST_SUITE=window-layout` with the normal runner and one unlocked
NativeAPI_Phone or NativeAPI_Desktop. The 46-scene fixture compares public C++/C
UI/worker calls with independent direct SDK enable/disable results, actual WMS
frames, native content bounds and object/manager/C fullscreen events. Primary
ArkUI layout uses actual `origBounds`; visible `bounds` can be clipped by the
other test children and are retained separately. No input
is injected. It holds delivery of a real SDK outcome to prove overlap refusal,
worker waiting, actual SDK child destruction, cancellation and late-result
isolation. Throw and non-promise cases are explicitly controlled SDK-boundary
faults. They do not claim a real system service failure. The computer main-window
SDK no-op and decorated-child inset are retained as platform limits.
