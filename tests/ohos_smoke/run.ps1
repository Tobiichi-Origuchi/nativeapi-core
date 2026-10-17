param(
  [Parameter(Mandatory=$true)][string]$DevEcoRoot,
  [string]$Device = '127.0.0.1:5555',
  [switch]$SkipBuild
)
$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'
$suite = $env:NATIVEAPI_TEST_SUITE
if (!$suite) { $suite = 'all' }
if ($suite -notin @('startup', 'native-close', 'drag-source', 'drop-target', 'shortcuts', 'clipboard', 'displays', 'all', 'picker', 'url', 'notification', 'notification-lifecycle', 'views', 'menus', 'accessibility', 'application', 'tray', 'integrations', 'occlusion', 'window-style', 'window-shape', 'window-drag', 'window-controls', 'window-aspect')) { throw 'NATIVEAPI_TEST_SUITE must be startup, native-close, drag-source, drop-target, shortcuts, clipboard, displays, all, picker, url, notification, notification-lifecycle, views, menus, accessibility, application, tray, integrations, occlusion, window-style, window-shape, window-drag, window-controls or window-aspect.' }
[Console]::OutputEncoding = [Text.Encoding]::UTF8
Set-Location $PSScriptRoot
if ($Device -notmatch '^127\.0\.0\.1:\d+$') { throw 'Use a disposable local emulator, never a physical device.' }
$hdc = Join-Path $DevEcoRoot 'sdk\default\openharmony\toolchains\hdc.exe'
$node = Join-Path $DevEcoRoot 'tools\node\node.exe'
$env:DEVECO_SDK_HOME = Join-Path $DevEcoRoot 'sdk'
$env:JAVA_HOME = Join-Path $DevEcoRoot 'jbr'
$env:PATH = "$DevEcoRoot\tools\node;$DevEcoRoot\tools\ohpm\bin;$env:JAVA_HOME\bin;$env:PATH"
$out = Join-Path $PSScriptRoot 'test-results'
New-Item -ItemType Directory -Force $out | Out-Null
function Invoke-Tool([string]$File, [string[]]$ToolArguments) {
  $ErrorActionPreference = 'Continue' # Windows PowerShell treats native stderr as an error stream.
  $output = & $File @ToolArguments 2>&1
  if ($LASTEXITCODE -ne 0) { throw ($output -join "`n") }
  return $output
}
function Device-Tool([string[]]$ToolArguments) {
  return Invoke-Tool $hdc (@('-t', $Device) + $ToolArguments)
}
Invoke-Tool $hdc @('tconn', $Device) | Out-Host
$arch = (Device-Tool @('shell', 'param', 'get', 'const.product.cpu.abilist')) -join ''
$model = (Device-Tool @('shell', 'param', 'get', 'const.product.model')) -join ''
if ($arch -notmatch 'x86_64' -or $model -notmatch 'emulator') { throw 'This runner only modifies a disposable x86_64 emulator.' }
$deviceType = ((Device-Tool @('shell', 'param', 'get', 'const.product.devicetype')) -join '').Trim()
$env:NATIVEAPI_OHOS_DESKTOP = '0'
if ($deviceType -eq '2in1') { $env:NATIVEAPI_OHOS_DESKTOP = '1' }
$buildType = Join-Path $out 'build-device-type.txt'
if (!$SkipBuild) {
  Invoke-Tool (Join-Path $DevEcoRoot 'tools\ohpm\bin\ohpm.bat') @('install', '--all') |
    Out-File (Join-Path $out 'ohpm.log') -Encoding utf8
  Invoke-Tool $node @((Join-Path $DevEcoRoot 'tools\hvigor\bin\hvigorw.js'), '--mode', 'module', '-p',
    'product=default', 'assembleHap', '--no-daemon') | Out-File (Join-Path $out 'build.log') -Encoding utf8
  $env:NATIVEAPI_OHOS_DESKTOP | Set-Content $buildType -Encoding ascii
} elseif (!(Test-Path $buildType) -or (Get-Content $buildType -Raw).Trim() -ne $env:NATIVEAPI_OHOS_DESKTOP) {
  throw 'The HAP was built for a different device lifecycle; rerun without -SkipBuild.'
}
$hap = Join-Path $PSScriptRoot 'entry\build\default\outputs\default\entry-default-unsigned.hap'
if (!(Test-Path $hap)) { throw "Missing HAP: $hap" }
$pythonTool = Get-Command python, python3 -ErrorAction SilentlyContinue | Select-Object -First 1
if (!$pythonTool) { throw 'Python 3 is required to validate the test HAP before installation.' }
Invoke-Tool $pythonTool.Source @('check_hap.py', $hap, '--desktop', $env:NATIVEAPI_OHOS_DESKTOP) | Out-Host
Device-Tool @('shell', 'aa', 'force-stop', 'dev.nativeapi.smoke') | Out-Host
Device-Tool @('install', $hap) | Out-Host
if ($suite -eq 'startup') {
  $startupPython = if ($env:NATIVEAPI_TEST_PYTHON) { $env:NATIVEAPI_TEST_PYTHON } else { $pythonTool.Source }
  Invoke-Tool $startupPython @('-u', 'run_startup_suite.py', $DevEcoRoot, '--device', $Device) |
    Tee-Object -FilePath (Join-Path $out 'startup-suite.log') | Out-Host
  exit 0
}
if ($suite -eq 'native-close') {
  $closePython = if ($env:NATIVEAPI_TEST_PYTHON) { $env:NATIVEAPI_TEST_PYTHON } else { 'python' }
  Invoke-Tool $closePython @('-u', 'run_native_close_suite.py', $DevEcoRoot, '--device', $Device) |
    Tee-Object -FilePath (Join-Path $out 'native-close-suite.log') | Out-Host
  exit 0
}
if ($suite -eq 'drag-source') {
  if ($env:NATIVEAPI_TEST_PYTHON) { $sourcePython = $env:NATIVEAPI_TEST_PYTHON } else { $sourcePython = $pythonTool.Source }
  Invoke-Tool $sourcePython @('-u', 'run_source_suite.py', $DevEcoRoot, '--device', $Device) |
    Tee-Object -FilePath (Join-Path $out 'source-suite.log') | Out-Host
  exit 0
}
if ($suite -eq 'drop-target') {
  if ($env:NATIVEAPI_TEST_PYTHON) { $dropPython = $env:NATIVEAPI_TEST_PYTHON } else { $dropPython = $pythonTool.Source }
  Invoke-Tool $dropPython @('-u', 'run_drop_suite.py', $DevEcoRoot, '--device', $Device) |
    Tee-Object -FilePath (Join-Path $out 'drop-suite.log') | Out-Host
  exit 0
}
if ($suite -eq 'shortcuts') {
  if (!$SkipBuild) {
    Invoke-Tool $pythonTool.Source @('-u', 'build_clipboard_peer.py', $DevEcoRoot) |
      Tee-Object -FilePath (Join-Path $out 'shortcut-peer-build.log') | Out-Host
  }
  if ($env:NATIVEAPI_TEST_PYTHON) { $shortcutPython = $env:NATIVEAPI_TEST_PYTHON } else { $shortcutPython = $pythonTool.Source }
  Invoke-Tool $shortcutPython @('-u', 'run_shortcut_suite.py', $DevEcoRoot, '--device', $Device) |
    Tee-Object -FilePath (Join-Path $out 'shortcut-suite.log') | Out-Host
  exit 0
}
if ($suite -eq 'clipboard') {
  if (!$SkipBuild) {
    Invoke-Tool $pythonTool.Source @('-u', 'build_clipboard_peer.py', $DevEcoRoot) |
      Tee-Object -FilePath (Join-Path $out 'clipboard-peer-build.log') | Out-Host
  }
  if ($env:NATIVEAPI_TEST_PYTHON) { $clipboardPython = $env:NATIVEAPI_TEST_PYTHON } else { $clipboardPython = $pythonTool.Source }
  Invoke-Tool $clipboardPython @('-u', 'run_clipboard_interop_suite.py', $DevEcoRoot, '--device', $Device) |
    Tee-Object -FilePath (Join-Path $out 'clipboard-interop.log') | Out-Host
  exit 0
}
if ($suite -eq 'displays') {
  if ($env:NATIVEAPI_TEST_PYTHON) { $displayPython = $env:NATIVEAPI_TEST_PYTHON } else { $displayPython = $pythonTool.Source }
  $displayMode = $env:NATIVEAPI_DISPLAY_MODE
  if (!$displayMode) { if ($deviceType -eq '2in1') { $displayMode = 'fullscreen' } else { $displayMode = 'rotation' } }
  Invoke-Tool $displayPython @('-u', 'run_display_suite.py', $DevEcoRoot, '--device', $Device, '--mode', $displayMode) |
    Tee-Object -FilePath (Join-Path $out 'display-suite.log') | Out-Host
  exit 0
}
if ($suite -eq 'notification-lifecycle') {
  Invoke-Tool $pythonTool.Source @('run_notification_lifecycle.py', $DevEcoRoot, '--device', $Device) |
    Tee-Object -FilePath (Join-Path $out 'notification-lifecycle.log') | Out-Host
  exit 0
}
if ($suite -eq 'notification') {
  $pythonTool = Get-Command python, python3 -ErrorAction SilentlyContinue | Select-Object -First 1
  if (!$pythonTool) { throw 'Python 3 is required for native notification assertions.' }
  Invoke-Tool $pythonTool.Source @('run_notification_suite.py', $DevEcoRoot, '--device', $Device) |
    Tee-Object -FilePath (Join-Path $out 'notification-suite.log') | Out-Host
  exit 0
}
if ($suite -eq 'url') {
  $pythonTool = Get-Command python, python3 -ErrorAction SilentlyContinue | Select-Object -First 1
  if (!$pythonTool) { throw 'Python 3 is required for native URL and browser assertions.' }
  Invoke-Tool $pythonTool.Source @('run_url_suite.py', $DevEcoRoot, '--device', $Device) |
    Tee-Object -FilePath (Join-Path $out 'url-suite.log') | Out-Host
  exit 0
}
if ($suite -eq 'window-aspect') {
  if ($env:NATIVEAPI_TEST_PYTHON) { $aspectPython = $env:NATIVEAPI_TEST_PYTHON } else { $aspectPython = $pythonTool.Source }
  Invoke-Tool $aspectPython @('-u', 'run_window_aspect_suite.py', $DevEcoRoot, '--device', $Device) |
    Tee-Object -FilePath (Join-Path $out 'window-aspect-suite.log') | Out-Host
  exit 0
}
if ($suite -eq 'window-controls') {
  if ($env:NATIVEAPI_TEST_PYTHON) { $controlsPython = $env:NATIVEAPI_TEST_PYTHON } else { $controlsPython = $pythonTool.Source }
  Invoke-Tool $controlsPython @('-u', 'run_window_controls_suite.py', $DevEcoRoot, '--device', $Device) |
    Tee-Object -FilePath (Join-Path $out 'window-controls-suite.log') | Out-Host
  exit 0
}
if ($suite -eq 'window-drag') {
  if ($env:NATIVEAPI_TEST_PYTHON) { $dragPython = $env:NATIVEAPI_TEST_PYTHON } else { $dragPython = $pythonTool.Source }
  Invoke-Tool $dragPython @('-u', 'run_window_drag_suite.py', $DevEcoRoot, '--device', $Device) |
    Tee-Object -FilePath (Join-Path $out 'window-drag-suite.log') | Out-Host
  exit 0
}
if ($suite -eq 'window-shape') {
  if ($env:NATIVEAPI_TEST_PYTHON) { $shapePython = $env:NATIVEAPI_TEST_PYTHON } else { $shapePython = $pythonTool.Source }
  Invoke-Tool $shapePython @('-u', 'run_window_shape_suite.py', $DevEcoRoot, '--device', $Device) |
    Tee-Object -FilePath (Join-Path $out 'window-shape-suite.log') | Out-Host
  exit 0
}
if ($suite -eq 'window-style') {
  if ($env:NATIVEAPI_TEST_PYTHON) { $stylePython = $env:NATIVEAPI_TEST_PYTHON } else { $stylePython = $pythonTool.Source }
  Invoke-Tool $stylePython @('-u', 'run_window_style_suite.py', $DevEcoRoot, '--device', $Device) |
    Tee-Object -FilePath (Join-Path $out 'window-style-suite.log') | Out-Host
  exit 0
}
if ($suite -eq 'occlusion') {
  if ($env:NATIVEAPI_TEST_PYTHON) { $occlusionPython = $env:NATIVEAPI_TEST_PYTHON } else { $occlusionPython = $pythonTool.Source }
  Invoke-Tool $occlusionPython @('-u', 'run_occlusion_suite.py', $DevEcoRoot, '--device', $Device) |
    Tee-Object -FilePath (Join-Path $out 'occlusion-suite.log') | Out-Host
  exit 0
}
if ($suite -eq 'integrations') {
  if ($env:NATIVEAPI_TEST_PYTHON) { $integrationPython = $env:NATIVEAPI_TEST_PYTHON } else { $integrationPython = $pythonTool.Source }
  Invoke-Tool $integrationPython @('-u', 'run_app_integration_suite.py', $DevEcoRoot, '--device', $Device) |
    Tee-Object -FilePath (Join-Path $out 'app-integration-suite.log') | Out-Host
  exit 0
}
if ($suite -eq 'tray') {
  if ($env:NATIVEAPI_TEST_PYTHON) { $trayPython = $env:NATIVEAPI_TEST_PYTHON } else { $trayPython = $pythonTool.Source }
  Invoke-Tool $trayPython @('-u', 'run_tray_suite.py', $DevEcoRoot, '--device', $Device) |
    Tee-Object -FilePath (Join-Path $out 'tray-suite.log') | Out-Host
  exit 0
}
if ($suite -eq 'application') {
  if ($env:NATIVEAPI_TEST_PYTHON) { $pythonTool = Get-Command $env:NATIVEAPI_TEST_PYTHON -ErrorAction Stop }
  Invoke-Tool $pythonTool.Source @('run_application_suite.py', $DevEcoRoot, '--device', $Device) |
    Tee-Object -FilePath (Join-Path $out 'application-suite.log') | Out-Host
  exit 0
}
if ($suite -eq 'accessibility') {
  Invoke-Tool $pythonTool.Source @('run_accessibility_suite.py', $DevEcoRoot, '--device', $Device) |
    Tee-Object -FilePath (Join-Path $out 'accessibility-suite.log') | Out-Host
  exit 0
}
$dump = (Device-Tool @('shell', 'bm', 'dump', '-n', 'dev.nativeapi.smoke')) -join "`n"
$token = [regex]::Match($dump, '"accessTokenId"\s*:\s*(\d+)').Groups[1].Value
if (!$token) { throw 'Could not find the test app token.' }
# This grant is only for the debug HAP in this disposable emulator. Production
# hosts must request READ_PASTEBOARD through their normal user permission flow.
Device-Tool @('shell', 'atm', 'perm', '-g', '-i', $token, '-p', 'ohos.permission.READ_PASTEBOARD') | Out-Host
$startArguments = @('shell', 'aa', 'start', '-a', 'EntryAbility', '-b', 'dev.nativeapi.smoke')
if ($suite -eq 'picker') { $startArguments += @('--ps', 'nativeapiSuite', 'picker') }
if ($suite -eq 'views') { $startArguments += @('--ps', 'nativeapiSuite', 'views') }
if ($suite -eq 'menus') { $startArguments += @('--ps', 'nativeapiSuite', 'menus') }
Device-Tool $startArguments | Out-Host
Start-Sleep -Milliseconds 700
$appPid = ((Device-Tool @('shell', 'pidof', 'dev.nativeapi.smoke')) -join '').Trim()
if ($appPid -notmatch '^\d+$') { throw 'Test app did not start.' }
if ($suite -eq 'menus') {
  Invoke-Tool $pythonTool.Source @('verify_menu_ui.py', $DevEcoRoot, '--device', $Device, '--pid', $appPid) |
    Tee-Object -FilePath (Join-Path $out 'menu-ui.log') | Out-Host
  exit 0
}
if ($suite -eq 'views') {
  Invoke-Tool $pythonTool.Source @('verify_view_ui.py', $DevEcoRoot, '--device', $Device, '--pid', $appPid) |
    Tee-Object -FilePath (Join-Path $out 'view-ui.log') | Out-Host
  exit 0
}
function Invoke-PickerTests {
  $pythonTool = Get-Command python, python3 -ErrorAction SilentlyContinue | Select-Object -First 1
  if (!$pythonTool) { throw 'Python 3 is required for guarded native picker UI assertions.' }
  Invoke-Tool $pythonTool.Source @('verify_picker_ui.py', $DevEcoRoot, '--device', $Device, '--pid', $appPid) |
    Tee-Object -FilePath (Join-Path $out 'picker-ui.log') | Out-Host
  $pickerLog = (Device-Tool @('shell', 'hilog', '-x', '-T', 'nativeapi-picker', '-P', $appPid)) -join "`n"
  $pickerLog | Out-File (Join-Path $out 'picker-runtime.log') -Encoding utf8
  if ($pickerLog -notmatch 'NATIVEAPI_PICKER_PASS') { throw $pickerLog }
  $pickerLog | Out-Host
}
if ($suite -eq 'picker') { Invoke-PickerTests; exit 0 }
$dialogsChecked = $false
for ($attempt = 0; $attempt -lt 120; $attempt++) {
  $log = (Device-Tool @('shell', 'hilog', '-x', '-T', 'nativeapi-test', '-P', $appPid)) -join "`n"
  $log | Out-File (Join-Path $out 'runtime.log') -Encoding utf8
  if ($log -match 'NATIVEAPI_OHOS_FAIL') { throw $log }
  if ($log -match 'NATIVEAPI_OHOS_PASS') { $log; Invoke-PickerTests; exit 0 }
  if (!$dialogsChecked -and $log -match 'NATIVEAPI_DIALOG_UI_READY') {
    $pythonTool = Get-Command python, python3 -ErrorAction SilentlyContinue | Select-Object -First 1
    if (!$pythonTool) { throw 'Python 3 is required for guarded native dialog UI assertions.' }
    Invoke-Tool $pythonTool.Source @('verify_dialog_ui.py', $DevEcoRoot, '--device', $Device, '--pid', $appPid) |
      Tee-Object -FilePath (Join-Path $out 'dialog-ui.log') | Out-Host
    $dialogsChecked = $true
  }
  Start-Sleep -Seconds 1
}
throw "No PASS marker from test process $appPid. See $out\runtime.log and the emulator fault logs."
