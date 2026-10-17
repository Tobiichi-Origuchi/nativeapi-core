#!/usr/bin/env python3
"""Guard native application controls and verify real theme pixels and Ability exit."""
import argparse
import json
import os
import re
import subprocess
import time
from pathlib import Path
from PIL import Image

p = argparse.ArgumentParser()
p.add_argument('deveco', type=Path)
p.add_argument('--device', default='127.0.0.1:5555')
p.add_argument('--pid', type=int, required=True)
p.add_argument('--action', choices=['full', 'required'], default='full')
a = p.parse_args()
assert re.fullmatch(r'127\.0\.0\.1:\d+', a.device)
suffix = '.exe' if os.name == 'nt' else ''
hdc = a.deveco / f'sdk/default/openharmony/toolchains/hdc{suffix}'
emulator = a.deveco / f'tools/emulator/Emulator{suffix}'
out = Path(__file__).resolve().parent / 'test-results/application-ui' / str(a.pid)
out.mkdir(parents=True, exist_ok=True)

def run(*cmd):
    return subprocess.check_output([str(x) for x in cmd], stdin=subprocess.DEVNULL,
                                   text=True, encoding='utf-8', stderr=subprocess.STDOUT, timeout=25)

def guest(*cmd):
    return run(hdc, '-t', a.device, 'shell', *cmd)

assert guest('param', 'get', 'const.product.model').strip() == 'emulator'
kind = guest('param', 'get', 'const.product.devicetype').strip()
instances = [i for i in json.loads(run(emulator, '-list', '-details')) if i['isRunning'] == 'true']
assert len(instances) == 1 and instances[0]['name'] == ('NativeAPI_Phone' if kind == 'phone' else 'NativeAPI_Desktop')
instance = instances[0]
assert instance['deviceType'] == kind
metadata = guest('bm', 'dump', '-n', 'dev.nativeapi.smoke')
uid = json.loads(metadata[metadata.index('{'):])['applicationInfo']['uid']

def identity():
    assert guest('pidof', 'dev.nativeapi.smoke').strip() == str(a.pid), 'Fixture exited or restarted'
    assert guest('cat', f'/proc/{a.pid}/cmdline').split('\0')[0] == 'dev.nativeapi.smoke'
    status = guest('cat', f'/proc/{a.pid}/status')
    found = re.search(r'^Uid:\s+(\d+)\s+(\d+)\s+(\d+)\s+(\d+)', status, re.M)
    assert found and all(int(v) == uid for v in found.groups())

identity()

def logs():
    text = guest('hilog', '-x', '-P', a.pid, '-T', 'nativeapi-app')
    (out / 'native.log').write_text(text)
    assert not re.search(r'\bFAIL\b|NATIVEAPI_APP_\S*FAIL', text), text
    return text

def wait(marker, alive=True):
    deadline = time.monotonic() + 35
    while time.monotonic() < deadline:
        if alive:
            identity()
        text = logs()
        if marker in text:
            return text
        time.sleep(.25)
    raise AssertionError(f'Timed out waiting for {marker}')

def layout(phase):
    identity()
    result = run(emulator, '-instance', instance['name'], '-uiLayout')
    assert 'Scenario simulation success' in result, result
    text = (Path(instance['instancePath']) / 'uiLayout/analysis.md').read_text()
    (out / f'{phase}-layout.md').write_text(text)
    assert 'NativeAPI Application Acceptance' in text
    return text

def rect(node):
    return tuple(map(int, re.findall(r'-?\d+', node['attributes']['bounds'])))

def owned(x, y, phase):
    identity()
    remote = '/data/local/tmp/nativeapi-application-owner.json'
    assert 'DumpLayout saved' in guest('uitest', 'dumpLayout', '-p', remote)
    path = out / f'{phase}-ownership.json'
    run(hdc, '-t', a.device, 'file', 'recv', remote, path)
    tree = json.loads(path.read_text())
    hits = [r for r in tree['children'] if rect(r)[0] <= x < rect(r)[2] and rect(r)[1] <= y < rect(r)[3]]
    assert len(hits) == 1 and hits[0]['attributes']['bundleName'] == 'dev.nativeapi.smoke'
    window = hits[0]['attributes']['hostWindowId']
    windows = guest('hidumper', '-s', 'WindowManagerService', '-a', '-a')
    (out / f'{phase}-windows.txt').write_text(windows)
    rows = [l.split() for l in windows.splitlines() if re.match(r'^\S+\s+\d+\s+\d+\s+' + re.escape(window) + r'\s+', l)]
    assert len(rows) == 1 and int(rows[0][2]) == a.pid and int(rows[0][7]) > 0
    (out / f'{phase}-point.json').write_text(json.dumps(dict(pid=a.pid, uid=uid, window=window, x=x, y=y)))

def box(text, label):
    matches = re.findall(re.escape(label) + r' \[id:(\d+)\] \[top: (\d+), left: (\d+), width: (\d+), height: (\d+)\]', text)
    assert len(matches) == 1, (label, matches)
    _, top, left, width, height = map(int, matches[0])
    assert width > 0 and height > 0
    return top, left, width, height

def click(label, phase):
    top, left, width, height = box(layout(phase), 'Button ' + label)
    x, y = left + width // 2, top + height // 2
    owned(x, y, phase)
    result = run(emulator, '-instance', instance['name'], '-click', f'{x} {y}')
    assert 'Scenario simulation success' in result, result


def theme(label):
    deadline = time.monotonic() + 25
    pattern = r'NATIVEAPI_APP_THEME ' + label + r' resource=([0-9a-f]+) mode=(-?\d+)'
    found = []
    while time.monotonic() < deadline:
        identity()
        raw = guest('hilog', '-x', '-P', a.pid, '-T', 'JSAPP')
        (out / 'sdk-theme.log').write_text(raw)
        found = re.findall(pattern, raw)
        if found:
            break
        time.sleep(.2)
    assert found, (label, raw)
    value, mode = int(found[-1][0], 16), int(found[-1][1])
    expected = (value >> 16 & 255, value >> 8 & 255, value & 255)
    top, left, width, height = box(layout(label), 'Text NativeAPI theme swatch')
    x, y = left + 8, top + 8
    assert x < left + width and y < top + height
    owned(x, y, label + '-pixel')
    folder = out / (label + '-screenshot-' + str(time.time_ns()))
    folder.mkdir(exist_ok=True)
    result = run(emulator, '-instance', instance['name'], '-screenshot', '-screenshotPath', folder)
    (out / f'{label}-screenshot-command.txt').write_text(result)
    picture = None
    deadline = time.monotonic() + 15
    while time.monotonic() < deadline:
        paths = list(folder.glob('*.png'))
        assert len(paths) <= 1, paths
        if paths:
            try:
                picture = Image.open(paths[0]).convert('RGB')
                break
            except OSError:
                pass  # The emulator returns before its PNG writer has finished.
        time.sleep(.1)
    assert picture is not None, (result, paths)
    assert picture.size == (int(instance['hw.lcd.single.width']), int(instance['hw.lcd.single.height']))
    pixels = [picture.getpixel((x + dx, y + dy)) for dx in range(3) for dy in range(3)]
    assert all(max(abs(actual[i] - expected[i]) for i in range(3)) <= 2 for actual in pixels), (label, expected, pixels)
    (out / f'{label}-pixels.json').write_text(json.dumps(dict(sdk_color=hex(value), mode=mode, expected=expected, pixels=pixels, point=[x,y])))
    print(f'PASS {kind} actual {label} appearance pixels {expected}, SDK configuration {mode}', flush=True)
    return expected, mode

wait('NATIVEAPI_APP_BASE_PASS')
if a.action == 'full':
    initial, _ = theme('initial')
    click('NativeAPI Light C++', 'light-click')
    light, light_mode = theme('light')
    assert min(light) >= 220 and light_mode == 1
    click('NativeAPI Dark C ABI', 'dark-click')
    dark, dark_mode = theme('dark')
    assert max(dark) <= 50 and dark_mode == 0 and dark != light
    click('NativeAPI System worker', 'system-click')
    system, _ = theme('system')
    assert system == initial, (initial, system)
    click('NativeAPI Cancel quit', 'cancel')
    wait('NATIVEAPI_APP_CANCEL_PASS')
    time.sleep(.5)
    identity()
    click('NativeAPI Deferred cancel', 'defer-cancel')
    wait('NATIVEAPI_APP_DEFER_CANCEL_PASS')
    time.sleep(.5)
    identity()
    click('NativeAPI Accept quit', 'accept')
    wait('NATIVEAPI_APP_ACCEPTING_QUIT code=99', alive=False)
    wait('NATIVEAPI_APP_EXIT code=99 exited=1', alive=False)
else:
    click('NativeAPI SDK terminate', 'required')
    wait('required native destruction cannot be vetoed or deferred', alive=False)
    wait('NATIVEAPI_APP_EXIT code=0 exited=1', alive=False)
text = wait('NATIVEAPI_APP_FINAL_PASS', alive=False)
wait('HOST_APPLICATION_LIFECYCLE destroy accepted=1', alive=False)
assert text.count('NATIVEAPI_APP_EXIT code=') == 1 and text.count('NATIVEAPI_APP_C_EVENT 1') == 1
for _ in range(30):
    windows = guest('hidumper', '-s', 'WindowManagerService', '-a', '-a')
    rows = [l for l in windows.splitlines() if re.match(r'^\S+\s+\d+\s+' + str(a.pid) + r'\s+\d+\s+', l)]
    if not rows:
        break
    time.sleep(.2)
assert not rows, windows
(out / 'ended-windows.txt').write_text(windows)
(out / 'ended-abilities.txt').write_text(guest('aa', 'dump', '-l'))
print(f'NATIVEAPI_APPLICATION_UI_PASS {kind} {a.action} PID {a.pid}; real native window removed', flush=True)
