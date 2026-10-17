#!/usr/bin/env bash
# Run only against a disposable local HarmonyOS emulator.
set -euo pipefail
deveco_root=${1:-${DEVECO_STUDIO_HOME:-/Applications/DevEco-Studio.app/Contents}}
device=${2:-127.0.0.1:5555}
mode=${3:-build}
suite=${NATIVEAPI_TEST_SUITE:-all}
[[ "$suite" == clipboard-boundaries || "$suite" == window-layout || "$suite" == window-mode-policy || "$suite" == window-fullscreen || "$suite" == window-mode-probe || "$suite" == window-title || "$suite" == window-title-probe || "$suite" == window-policies || "$suite" == window-shadow-main || "$suite" == window-shadow || "$suite" == window-shadow-probe || "$suite" == foundation || "$suite" == tooltip || "$suite" == window-parent || "$suite" == window-manager || "$suite" == preferences || "$suite" == startup || "$suite" == native-close || "$suite" == drag-source || "$suite" == drop-target || "$suite" == shortcuts || "$suite" == clipboard || "$suite" == displays || "$suite" == all || "$suite" == picker || "$suite" == url || "$suite" == notification || "$suite" == notification-lifecycle || "$suite" == views || "$suite" == menus || "$suite" == accessibility || "$suite" == application || "$suite" == tray || "$suite" == integrations || "$suite" == occlusion || "$suite" == window-style || "$suite" == window-shape || "$suite" == window-drag || "$suite" == window-controls || "$suite" == window-aspect ]] || { echo 'NATIVEAPI_TEST_SUITE must be clipboard-boundaries, window-layout, window-mode-policy, window-fullscreen, window-mode-probe, window-title, window-title-probe, window-policies, window-shadow-main, window-shadow, window-shadow-probe, foundation, tooltip, window-parent, window-manager, preferences, startup, native-close, drag-source, drop-target, shortcuts, clipboard, displays, all, picker, url, notification, notification-lifecycle, views, menus, accessibility, application, tray, integrations, occlusion, window-style, window-shape, window-drag, window-controls or window-aspect.' >&2; exit 2; }
[[ "$device" =~ ^127\.0\.0\.1:[0-9]+$ ]] || { echo 'Use a disposable local emulator, never a physical device.' >&2; exit 2; }
[[ "$mode" == build || "$mode" == --skip-build ]] || { echo 'Third argument must be --skip-build or build.' >&2; exit 2; }
cd "$(dirname "$0")"
hdc="$deveco_root/sdk/default/openharmony/toolchains/hdc"
node="$deveco_root/tools/node/bin/node"
[[ -x "$node" ]] || node="$deveco_root/tools/node/node"
[[ -x "$hdc" && -x "$node" ]] || { echo "Missing DevEco tools under $deveco_root" >&2; exit 2; }
export DEVECO_SDK_HOME="$deveco_root/sdk"
export JAVA_HOME="$deveco_root/jbr/Contents/Home"
[[ -d "$JAVA_HOME" ]] || export JAVA_HOME="$deveco_root/jbr"
export PATH="$(dirname "$hdc"):$(dirname "$node"):$deveco_root/tools/ohpm/bin:$JAVA_HOME/bin:$PATH"
# Hvigor may clear the root build directory while assembling a fresh project.
out="$PWD/test-results"
mkdir -p "$out"
target=("$hdc" -t "$device")
"$hdc" tconn "$device"
arch=$("${target[@]}" shell param get const.product.cpu.abilist | tr -d '\r')
model=$("${target[@]}" shell param get const.product.model | tr -d '\r')
[[ "$arch" =~ (x86_64|arm64-v8a|aarch64) && "$model" == *emulator* ]] || {
  echo 'Only disposable ARM64/x86_64 emulators are supported.' >&2; exit 2;
}
device_type=$("${target[@]}" shell param get const.product.devicetype | tr -d '\r\n ')
export NATIVEAPI_OHOS_DESKTOP=0
[[ "$device_type" != 2in1 ]] || export NATIVEAPI_OHOS_DESKTOP=1
build_type="$out/build-device-type.txt"
if [[ "$mode" == build ]]; then
  "$deveco_root/tools/ohpm/bin/ohpm" install --all > "$out/ohpm.log" 2>&1
  "$node" "$deveco_root/tools/hvigor/bin/hvigorw.js" --mode module -p product=default assembleHap --no-daemon > "$out/build.log" 2>&1
  printf '%s\n' "$NATIVEAPI_OHOS_DESKTOP" > "$build_type"
elif [[ ! -f "$build_type" || "$(cat "$build_type")" != "$NATIVEAPI_OHOS_DESKTOP" ]]; then
  echo 'The HAP was built for a different device lifecycle; rerun without --skip-build.' >&2
  exit 2
fi
hap="$PWD/entry/build/default/outputs/default/entry-default-unsigned.hap"
[[ -f "$hap" ]] || { echo "Missing HAP: $hap" >&2; exit 1; }
python3 check_hap.py "$hap" --desktop "$NATIVEAPI_OHOS_DESKTOP"
"${target[@]}" shell aa force-stop dev.nativeapi.smoke
"${target[@]}" install "$hap"
if [[ "$suite" == window-layout ]]; then
  "${NATIVEAPI_TEST_PYTHON:-python3}" -u run_window_layout_suite.py "$deveco_root" --device "$device" > "$out/window-layout-suite.log" 2>&1 || {
    cat "$out/window-layout-suite.log"; exit 1;
  }
  cat "$out/window-layout-suite.log"
  exit 0
fi
if [[ "$suite" == window-mode-policy ]]; then
  "${NATIVEAPI_TEST_PYTHON:-python3}" -u run_window_mode_suite.py "$deveco_root" --device "$device" > "$out/window-mode-suite.log" 2>&1 || {
    cat "$out/window-mode-suite.log"; exit 1;
  }
  cat "$out/window-mode-suite.log"
  exit 0
fi
if [[ "$suite" == window-fullscreen ]]; then
  "${NATIVEAPI_TEST_PYTHON:-python3}" -u run_window_fullscreen_suite.py "$deveco_root" --device "$device" > "$out/window-fullscreen-suite.log" 2>&1 || {
    cat "$out/window-fullscreen-suite.log"; exit 1;
  }
  cat "$out/window-fullscreen-suite.log"
  exit 0
fi
if [[ "$suite" == window-mode-probe ]]; then
  "${NATIVEAPI_TEST_PYTHON:-python3}" -u run_window_mode_probe.py "$deveco_root" --device "$device" > "$out/window-mode-probe.log" 2>&1 || {
    cat "$out/window-mode-probe.log"; exit 1;
  }
  cat "$out/window-mode-probe.log"
  exit 0
fi
if [[ "$suite" == window-title ]]; then
  "${NATIVEAPI_TEST_PYTHON:-python3}" -u run_window_title_suite.py "$deveco_root" --device "$device" > "$out/window-title-suite.log" 2>&1 || {
    cat "$out/window-title-suite.log"; exit 1;
  }
  cat "$out/window-title-suite.log"
  exit 0
fi
if [[ "$suite" == window-title-probe ]]; then
  "${NATIVEAPI_TEST_PYTHON:-python3}" -u run_window_title_probe.py "$deveco_root" --device "$device" > "$out/window-title-probe.log" 2>&1 || {
    cat "$out/window-title-probe.log"; exit 1;
  }
  cat "$out/window-title-probe.log"
  exit 0
fi
if [[ "$suite" == window-policies ]]; then
  "${NATIVEAPI_TEST_PYTHON:-python3}" -u run_window_policies_suite.py "$deveco_root" --device "$device" > "$out/window-policies-suite.log" 2>&1 || {
    cat "$out/window-policies-suite.log"; exit 1;
  }
  cat "$out/window-policies-suite.log"
  exit 0
fi
if [[ "$suite" == window-shadow-main ]]; then
  "${NATIVEAPI_TEST_PYTHON:-python3}" -u run_window_shadow_main_suite.py "$deveco_root" --device "$device" > "$out/window-shadow-main-suite.log" 2>&1 || {
    cat "$out/window-shadow-main-suite.log"; exit 1;
  }
  cat "$out/window-shadow-main-suite.log"
  exit 0
fi
if [[ "$suite" == window-shadow ]]; then
  "${NATIVEAPI_TEST_PYTHON:-python3}" -u run_window_shadow_suite.py "$deveco_root" --device "$device" > "$out/window-shadow-suite.log" 2>&1 || {
    cat "$out/window-shadow-suite.log"; exit 1;
  }
  cat "$out/window-shadow-suite.log"
  exit 0
fi
if [[ "$suite" == window-shadow-probe ]]; then
  "${NATIVEAPI_TEST_PYTHON:-python3}" -u run_window_shadow_probe.py "$deveco_root" --device "$device" > "$out/window-shadow-probe.log" 2>&1 || {
    cat "$out/window-shadow-probe.log"; exit 1;
  }
  cat "$out/window-shadow-probe.log"
  exit 0
fi
if [[ "$suite" == foundation ]]; then
  "${NATIVEAPI_TEST_PYTHON:-python3}" -u run_foundation_suite.py "$deveco_root" --device "$device" > "$out/foundation-suite.log" 2>&1 || {
    cat "$out/foundation-suite.log"; exit 1;
  }
  cat "$out/foundation-suite.log"
  exit 0
fi
if [[ "$suite" == tooltip ]]; then
  "${NATIVEAPI_TEST_PYTHON:-python3}" -u run_tooltip_suite.py "$deveco_root" --device "$device" > "$out/tooltip-suite.log" 2>&1 || {
    cat "$out/tooltip-suite.log"; exit 1;
  }
  cat "$out/tooltip-suite.log"
  exit 0
fi
if [[ "$suite" == window-parent ]]; then
  "${NATIVEAPI_TEST_PYTHON:-python3}" -u run_window_parent_suite.py "$deveco_root" --device "$device" > "$out/window-parent-suite.log" 2>&1 || {
    cat "$out/window-parent-suite.log"; exit 1;
  }
  cat "$out/window-parent-suite.log"
  exit 0
fi
if [[ "$suite" == window-manager ]]; then
  "${NATIVEAPI_TEST_PYTHON:-python3}" -u run_window_manager_suite.py "$deveco_root" --device "$device" > "$out/window-manager-suite.log" 2>&1 || {
    cat "$out/window-manager-suite.log"; exit 1;
  }
  cat "$out/window-manager-suite.log"
  exit 0
fi
if [[ "$suite" == preferences ]]; then
  "${NATIVEAPI_TEST_PYTHON:-python3}" -u run_preferences_suite.py "$deveco_root" --device "$device" > "$out/preferences-suite.log" 2>&1 || {
    cat "$out/preferences-suite.log"; exit 1;
  }
  cat "$out/preferences-suite.log"
  exit 0
fi
if [[ "$suite" == startup ]]; then
  "${NATIVEAPI_TEST_PYTHON:-python3}" -u run_startup_suite.py "$deveco_root" --device "$device" > "$out/startup-suite.log" 2>&1 || {
    cat "$out/startup-suite.log"; exit 1;
  }
  cat "$out/startup-suite.log"
  exit 0
fi
if [[ "$suite" == native-close ]]; then
  "${NATIVEAPI_TEST_PYTHON:-python3}" -u run_native_close_suite.py "$deveco_root" --device "$device" > "$out/native-close-suite.log" 2>&1 || {
    cat "$out/native-close-suite.log"; exit 1;
  }
  cat "$out/native-close-suite.log"
  exit 0
fi
if [[ "$suite" == drag-source ]]; then
  "${NATIVEAPI_TEST_PYTHON:-python3}" -u run_source_suite.py "$deveco_root" --device "$device" > "$out/source-suite.log" 2>&1 || {
    cat "$out/source-suite.log"; exit 1;
  }
  cat "$out/source-suite.log"
  exit 0
fi
if [[ "$suite" == drop-target ]]; then
  "${NATIVEAPI_TEST_PYTHON:-python3}" -u run_drop_suite.py "$deveco_root" --device "$device" > "$out/drop-suite.log" 2>&1 || {
    cat "$out/drop-suite.log"; exit 1;
  }
  cat "$out/drop-suite.log"
  exit 0
fi
if [[ "$suite" == shortcuts ]]; then
  if [[ "$mode" == build ]]; then
    python3 -u build_clipboard_peer.py "$deveco_root" > "$out/shortcut-peer-build.log" 2>&1 || {
      cat "$out/shortcut-peer-build.log"; exit 1;
    }
  fi
  "${NATIVEAPI_TEST_PYTHON:-python3}" -u run_shortcut_suite.py "$deveco_root" --device "$device" > "$out/shortcut-suite.log" 2>&1 || {
    cat "$out/shortcut-suite.log"; exit 1;
  }
  cat "$out/shortcut-suite.log"
  exit 0
fi
if [[ "$suite" == clipboard || "$suite" == clipboard-boundaries ]]; then
  if [[ "$mode" == build ]]; then
    python3 -u build_clipboard_peer.py "$deveco_root" > "$out/clipboard-peer-build.log" 2>&1 || {
      cat "$out/clipboard-peer-build.log"; exit 1;
    }
  fi
  clipboard_driver=run_clipboard_interop_suite.py
  [[ "$suite" != clipboard-boundaries ]] || clipboard_driver=run_clipboard_boundary_suite.py
  "${NATIVEAPI_TEST_PYTHON:-python3}" -u "$clipboard_driver" "$deveco_root" --device "$device" > "$out/clipboard-interop.log" 2>&1 || {
    cat "$out/clipboard-interop.log"; exit 1;
  }
  cat "$out/clipboard-interop.log"
  exit 0
fi
if [[ "$suite" == displays ]]; then
  display_mode=${NATIVEAPI_DISPLAY_MODE:-rotation}
  [[ "$device_type" != 2in1 || -n "${NATIVEAPI_DISPLAY_MODE:-}" ]] || display_mode=fullscreen
  "${NATIVEAPI_TEST_PYTHON:-python3}" -u run_display_suite.py "$deveco_root" --device "$device" --mode "$display_mode" > "$out/display-suite.log" 2>&1 || {
    cat "$out/display-suite.log"; exit 1;
  }
  cat "$out/display-suite.log"
  exit 0
fi
if [[ "$suite" == notification-lifecycle ]]; then
  python3 -u run_notification_lifecycle.py "$deveco_root" --device "$device" > "$out/notification-lifecycle.log" 2>&1 || {
    cat "$out/notification-lifecycle.log"; exit 1;
  }
  cat "$out/notification-lifecycle.log"
  exit 0
fi
if [[ "$suite" == notification ]]; then
  python3 -u run_notification_suite.py "$deveco_root" --device "$device" > "$out/notification-suite.log" 2>&1 || {
    cat "$out/notification-suite.log"; exit 1;
  }
  cat "$out/notification-suite.log"
  exit 0
fi
if [[ "$suite" == url ]]; then
  python3 -u run_url_suite.py "$deveco_root" --device "$device" > "$out/url-suite.log" 2>&1 || {
    cat "$out/url-suite.log"; exit 1;
  }
  cat "$out/url-suite.log"
  exit 0
fi
if [[ "$suite" == window-aspect ]]; then
  "${NATIVEAPI_TEST_PYTHON:-python3}" -u run_window_aspect_suite.py "$deveco_root" --device "$device" > "$out/window-aspect-suite.log" 2>&1 || {
    cat "$out/window-aspect-suite.log"; exit 1;
  }
  cat "$out/window-aspect-suite.log"
  exit 0
fi
if [[ "$suite" == window-controls ]]; then
  "${NATIVEAPI_TEST_PYTHON:-python3}" -u run_window_controls_suite.py "$deveco_root" --device "$device" > "$out/window-controls-suite.log" 2>&1 || {
    cat "$out/window-controls-suite.log"; exit 1;
  }
  cat "$out/window-controls-suite.log"
  exit 0
fi
if [[ "$suite" == window-drag ]]; then
  "${NATIVEAPI_TEST_PYTHON:-python3}" -u run_window_drag_suite.py "$deveco_root" --device "$device" > "$out/window-drag-suite.log" 2>&1 || {
    cat "$out/window-drag-suite.log"; exit 1;
  }
  cat "$out/window-drag-suite.log"
  exit 0
fi
if [[ "$suite" == window-shape ]]; then
  "${NATIVEAPI_TEST_PYTHON:-python3}" -u run_window_shape_suite.py "$deveco_root" --device "$device" > "$out/window-shape-suite.log" 2>&1 || {
    cat "$out/window-shape-suite.log"; exit 1;
  }
  cat "$out/window-shape-suite.log"
  exit 0
fi
if [[ "$suite" == window-style ]]; then
  "${NATIVEAPI_TEST_PYTHON:-python3}" -u run_window_style_suite.py "$deveco_root" --device "$device" > "$out/window-style-suite.log" 2>&1 || {
    cat "$out/window-style-suite.log"; exit 1;
  }
  cat "$out/window-style-suite.log"
  exit 0
fi
if [[ "$suite" == occlusion ]]; then
  "${NATIVEAPI_TEST_PYTHON:-python3}" -u run_occlusion_suite.py "$deveco_root" --device "$device" > "$out/occlusion-suite.log" 2>&1 || {
    cat "$out/occlusion-suite.log"; exit 1;
  }
  cat "$out/occlusion-suite.log"
  exit 0
fi
if [[ "$suite" == integrations ]]; then
  "${NATIVEAPI_TEST_PYTHON:-python3}" -u run_app_integration_suite.py "$deveco_root" --device "$device" > "$out/app-integration-suite.log" 2>&1 || {
    cat "$out/app-integration-suite.log"; exit 1;
  }
  cat "$out/app-integration-suite.log"
  exit 0
fi
if [[ "$suite" == tray ]]; then
  "${NATIVEAPI_TEST_PYTHON:-python3}" -u run_tray_suite.py "$deveco_root" --device "$device" > "$out/tray-suite.log" 2>&1 || {
    cat "$out/tray-suite.log"; exit 1;
  }
  cat "$out/tray-suite.log"
  exit 0
fi
if [[ "$suite" == application ]]; then
  "${NATIVEAPI_TEST_PYTHON:-python3}" -u run_application_suite.py "$deveco_root" --device "$device" > "$out/application-suite.log" 2>&1 || {
    cat "$out/application-suite.log"; exit 1;
  }
  cat "$out/application-suite.log"
  exit 0
fi
if [[ "$suite" == accessibility ]]; then
  python3 -u run_accessibility_suite.py "$deveco_root" --device "$device" > "$out/accessibility-suite.log" 2>&1 || {
    cat "$out/accessibility-suite.log"; exit 1;
  }
  cat "$out/accessibility-suite.log"
  exit 0
fi
dump=$("${target[@]}" shell bm dump -n dev.nativeapi.smoke)
token=$(printf '%s' "$dump" | python3 -c 'import re,sys; m=re.search(r"\"accessTokenId\"\s*:\s*(\d+)",sys.stdin.read()); print(m[1] if m else "")')
[[ -n "$token" ]] || { echo 'Could not find the debug app token.' >&2; exit 1; }
# Debug-emulator grant only. Production hosts must use the normal permission flow.
"${target[@]}" shell atm perm -g -i "$token" -p ohos.permission.READ_PASTEBOARD
start=(shell aa start -a EntryAbility -b dev.nativeapi.smoke)
[[ "$suite" != picker ]] || start+=(--ps nativeapiSuite picker)
[[ "$suite" != views ]] || start+=(--ps nativeapiSuite views)
[[ "$suite" != menus ]] || start+=(--ps nativeapiSuite menus)
"${target[@]}" "${start[@]}"
sleep 1
app_pid=$("${target[@]}" shell pidof dev.nativeapi.smoke | tr -d '\r\n ')
[[ "$app_pid" =~ ^[0-9]+$ ]] || { echo 'Test app did not start.' >&2; exit 1; }
if [[ "$suite" == menus ]]; then
  python3 -u verify_menu_ui.py "$deveco_root" --device "$device" --pid "$app_pid" > "$out/menu-ui.log" 2>&1 || {
    cat "$out/menu-ui.log"; exit 1;
  }
  cat "$out/menu-ui.log"
  exit 0
fi
if [[ "$suite" == views ]]; then
  python3 -u verify_view_ui.py "$deveco_root" --device "$device" --pid "$app_pid" > "$out/view-ui.log" 2>&1 || {
    cat "$out/view-ui.log"; exit 1;
  }
  cat "$out/view-ui.log"
  exit 0
fi
run_picker_tests() {
  python3 verify_picker_ui.py "$deveco_root" --device "$device" --pid "$app_pid" > "$out/picker-ui.log" 2>&1 || {
    cat "$out/picker-ui.log"; return 1;
  }
  "${target[@]}" shell hilog -x -T nativeapi-picker -P "$app_pid" > "$out/picker-runtime.log"
  cat "$out/picker-ui.log"
  cat "$out/picker-runtime.log"
  grep -q NATIVEAPI_PICKER_PASS "$out/picker-runtime.log"
}
if [[ "$suite" == picker ]]; then
  run_picker_tests
  exit $?
fi
dialogs_checked=false
for ((attempt=0; attempt<120; attempt++)); do
  "${target[@]}" shell hilog -x -T nativeapi-test -P "$app_pid" > "$out/runtime.log"
  if grep -q NATIVEAPI_OHOS_FAIL "$out/runtime.log"; then cat "$out/runtime.log"; exit 1; fi
  if grep -q NATIVEAPI_OHOS_PASS "$out/runtime.log"; then
    cat "$out/runtime.log"
    run_picker_tests
    exit $?
  fi
  if [[ "$dialogs_checked" == false ]] && grep -q NATIVEAPI_DIALOG_UI_READY "$out/runtime.log"; then
    python3 verify_dialog_ui.py "$deveco_root" --device "$device" --pid "$app_pid" > "$out/dialog-ui.log" 2>&1 || {
      cat "$out/dialog-ui.log"; exit 1;
    }
    cat "$out/dialog-ui.log"
    dialogs_checked=true
  fi
  sleep 1
done
echo "No PASS marker from process $app_pid; see $out/runtime.log and the emulator fault logs." >&2
exit 1
