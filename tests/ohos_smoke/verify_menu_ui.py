#!/usr/bin/env python3
"""Drive native menus inside a named disposable guest; never inject host input."""
import argparse
import json
import os
import re
import subprocess
import time
from pathlib import Path

parser = argparse.ArgumentParser()
parser.add_argument('deveco', type=Path)
parser.add_argument('--device', default='127.0.0.1:5555')
parser.add_argument('--pid', required=True, type=int)
args = parser.parse_args()
assert re.fullmatch(r'127\.0\.0\.1:\d+', args.device), 'Only disposable local emulators'
suffix = '.exe' if os.name == 'nt' else ''
hdc = args.deveco / f'sdk/default/openharmony/toolchains/hdc{suffix}'
emulator = args.deveco / f'tools/emulator/Emulator{suffix}'
out = Path(__file__).resolve().parent / 'test-results/menu-ui' / str(args.pid)
out.mkdir(parents=True, exist_ok=True)


def run(*command):
    return subprocess.check_output([str(x) for x in command], text=True, encoding='utf-8',
                                   stderr=subprocess.STDOUT, stdin=subprocess.DEVNULL, timeout=20)


def guest(*command):
    return run(hdc, '-t', args.device, 'shell', *command)


assert guest('param', 'get', 'const.product.model').strip() == 'emulator'
kind = guest('param', 'get', 'const.product.devicetype').strip()
instances = json.loads(run(emulator, '-list', '-details'))
running = [x for x in instances if x['isRunning'] == 'true']
assert len(running) == 1 and running[0]['name'].startswith('NativeAPI_')
instance = running[0]
assert instance['deviceType'] == kind
assert int(guest('pidof', 'dev.nativeapi.smoke').strip()) == args.pid
assert guest('cat', f'/proc/{args.pid}/cmdline').split('\0')[0] == 'dev.nativeapi.smoke'
metadata = guest('bm', 'dump', '-n', 'dev.nativeapi.smoke')
uid = json.loads(metadata[metadata.index('{'):])['applicationInfo']['uid']
status = guest('cat', f'/proc/{args.pid}/status')
match = re.search(r'^Uid:\s+(\d+)\s+(\d+)\s+(\d+)\s+(\d+)', status, re.M)
assert match and all(int(value) == uid for value in match.groups())


def layout(phase):
    assert guest('pidof', 'dev.nativeapi.smoke').strip() == str(args.pid), 'Fixture exited'
    result = run(emulator, '-instance', instance['name'], '-uiLayout')
    assert 'Scenario simulation success' in result, result
    text = (Path(instance['instancePath']) / 'uiLayout/analysis.md').read_text(encoding='utf-8')
    (out / f'{phase}-layout.md').write_text(text, encoding='utf-8')
    return text


def wait_phase(phase, expected):
    deadline = time.monotonic() + 40
    while time.monotonic() < deadline:
        assert int(guest('pidof', 'dev.nativeapi.smoke').strip()) == args.pid
        log = guest('hilog', '-x', '-T', 'nativeapi-menu', '-P', args.pid)
        assert not re.search(r'\bFAIL\b|NATIVEAPI_MENU_FAIL', log), log[-8000:]
        if f'NATIVEAPI_MENU_UI_READY {phase}' in log:
            text = layout(phase)
            if expected in text:
                return text
        time.sleep(0.25)
    raise AssertionError(f'Native views did not reach {phase}')


def click(phase, pattern):
    text = layout(phase)
    assert 'NativeAPI ' in text
    matches = re.findall(pattern + r' \[id:(\d+)\] \[top: (\d+), left: (\d+), width: (\d+), height: (\d+)\]', text)
    assert len(matches) == 1, f'Expected one {pattern}: {text}'
    identifier, top, left, width, height = map(int, matches[0])
    assert width > 0 and height > 0
    x, y = left + width // 2, top + height // 2
    # Confirm native ownership immediately before each press, including overlays.
    assert int(guest('pidof', 'dev.nativeapi.smoke').strip()) == args.pid
    remote = '/data/local/tmp/nativeapi-menu-ownership.json'
    guest('uitest', 'dumpLayout', '-p', remote)
    target = out / f'{phase}-ownership.json'
    run(hdc, '-t', args.device, 'file', 'recv', remote, target)
    tree = json.loads(target.read_text(encoding='utf-8'))
    def walk(node):
        yield node
        for child in node.get('children', []):
            yield from walk(child)
    entries = {node['attributes']['text']: node for node in walk(tree)
               if node.get('attributes', {}).get('type') == 'MenuItem'}
    if phase in ('disabled', 'more'):
        assert entries['NativeAPI Disabled']['attributes']['enabled'] == 'false'
        assert entries['NativeAPI Radio A']['attributes']['selected'] == 'false'
        assert entries['NativeAPI Radio B']['attributes']['selected'] == 'true'
        toggle = entries['NativeAPI Toggle']
        assert toggle['attributes']['selected'] == ('true' if phase == 'more' else 'false')
        if phase == 'disabled':
            assert any(n.get('attributes', {}).get('text', '').strip() == '—' for n in walk(toggle))
    hits = []
    for root in tree['children']:
        attributes = root['attributes']
        bounds = re.fullmatch(r'\[(-?\d+),(-?\d+)\]\[(-?\d+),(-?\d+)\]', attributes['bounds'])
        assert bounds
        a, b, c, d = map(int, bounds.groups())
        if a <= x < c and b <= y < d:
            hits.append(attributes)
    assert len(hits) == 1 and hits[0].get('bundleName') == 'dev.nativeapi.smoke', \
        f'Press point is not owned by the native test app: {hits}'
    dump = guest('hidumper', '-s', 'WindowManagerService', '-a', '-a')
    (out / f'{phase}-windows.txt').write_text(dump, encoding='utf-8')
    own_ids = set()
    for line in dump.splitlines():
        match = re.match(r'^\S+\s+\d+\s+(\d+)\s+(\d+)\s+', line)
        if match and int(match[1]) == args.pid:
            own_ids.add(match[2])
    assert hits[0].get('hostWindowId') in own_ids
    result = run(emulator, '-instance', instance['name'], '-click', f'{x} {y}')
    assert 'Scenario simulation success' in result, result
    time.sleep(0.35)


def anchor_geometry(text):
    log = guest('hilog', '-x', '-P', args.pid)
    found = re.findall(r'NATIVEAPI_MENU_ANCHOR requested=(\{[^\n]+?\}) actual=(\{[^\n]+?\})', log)
    assert found, log[-3000:]
    requested, actual = map(json.loads, found[-1])
    assert abs(requested['x'] - actual['x']) < 1 and abs(requested['y'] - actual['y']) < 1
    rectangles = set(tuple(map(int, r)) for r in re.findall(r'- Menu \[top: (\d+), left: (\d+), width: (\d+), height: (\d+)\]', text))
    assert len(rectangles) == 1, rectangles
    top, left, width, height = next(iter(rectangles))
    scale = float(instance['hw.lcd.density']) / 160
    requested_left = requested['x'] * scale
    if abs(left - requested_left) >= 2 * scale:
        wrappers = set(tuple(map(int, r)) for r in re.findall(r'- MenuWrapper \[top: (\d+), left: (\d+), width: (\d+), height: (\d+)\]', text))
        assert len(wrappers) <= 1, wrappers
        if wrappers:
            viewport_top, viewport_left, viewport_width, viewport_height = next(iter(wrappers))
        else:
            viewport_left, viewport_width = 0, int(instance['hw.lcd.single.width'])
        assert requested_left + width > viewport_left + viewport_width - 16 * scale
        assert abs(left + width - viewport_left - viewport_width) <= 32 * scale, (rectangles, wrappers)
    bottom = requested['y'] + max(1, requested['height'])
    assert 0 <= top / scale - bottom <= 24, (rectangles, requested, scale)


text = wait_phase('toggle', 'NativeAPI Toggle')
assert 'NativeAPI Disabled' in text and 'NativeAPI More' in text
anchor_geometry(text)
run(emulator, '-instance', instance['name'], '-screenshot', '-screenshotPath', out)
click('disabled', r'MenuItem NativeAPI Disabled')
click('toggle', r'MenuItem NativeAPI Toggle')
print('PASS real ArkUI menu accepts checkbox and ignores disabled item', flush=True)
text = wait_phase('submenu', 'NativeAPI Updated')
assert 'NativeAPI Must Be Removed' not in text
click('more', r'MenuItem NativeAPI More')
for _ in range(80):
    text = layout('nested')
    if 'NativeAPI Nested Action' in text:
        break
    time.sleep(0.25)
else:
    raise AssertionError('Native submenu did not appear')
click('nested', r'MenuItem NativeAPI Nested Action')
print('PASS dynamic native label/removal and actual submenu interaction', flush=True)
text = wait_phase('c', 'NativeAPI C Menu')
anchor_geometry(text)
click('c', r'MenuItem NativeAPI C Menu')
wait_phase('submenu-close', 'NativeAPI More')
click('submenu-close', r'MenuItem NativeAPI More')
print('PASS explicit submenu close hides native popup hierarchy', flush=True)
for _ in range(90):
    log = guest('hilog', '-x', '-T', 'nativeapi-menu', '-P', args.pid)
    assert not re.search(r'\bFAIL\b|NATIVEAPI_MENU_FAIL', log), log[-8000:]
    if 'NATIVEAPI_MENU_PASS' in log:
        text = layout('finished')
        assert re.search(r'NATIVEAPI_MENU_PASS \(\d+ passed, 0 failed\)', text)
        assert 'MenuItem NativeAPI ' not in text
        (out / 'native.log').write_text(log, encoding='utf-8')
        print('PASS C ABI native menu click, programmatic close and destruction cleanup', flush=True)
        print('NATIVEAPI_MENU_UI_PASS (4 passed, 0 failed)', flush=True)
        break
    time.sleep(0.25)
else:
    raise AssertionError('Native menu lifecycle tests did not finish')
