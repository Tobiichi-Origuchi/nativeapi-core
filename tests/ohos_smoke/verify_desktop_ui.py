#!/usr/bin/env python3
"""Exercise only the disposable HarmonyOS guest; never inject host desktop input."""
import argparse
import json
import re
import subprocess
import time
from pathlib import Path

parser = argparse.ArgumentParser()
parser.add_argument('deveco', type=Path)
parser.add_argument('--device', default='127.0.0.1:5555')
parser.add_argument('--instance', default='NativeAPI_Desktop')
args = parser.parse_args()
assert re.fullmatch(r'127\.0\.0\.1:\d+', args.device), 'Only local emulator targets are allowed'
assert args.instance.startswith('NativeAPI_'), 'Use an isolated NativeAPI test emulator'
hdc = args.deveco / 'sdk/default/openharmony/toolchains/hdc'
emulator = args.deveco / 'tools/emulator/Emulator'
out = Path(__file__).resolve().parent / 'test-results/desktop-ui'
out.mkdir(parents=True, exist_ok=True)


def run(*command):
    return subprocess.check_output([str(x) for x in command], text=True, stderr=subprocess.STDOUT,
                                   stdin=subprocess.DEVNULL, timeout=30)


def guest(*command):
    return run(hdc, '-t', args.device, 'shell', *command)


run(hdc, 'tconn', args.device)
assert guest('param', 'get', 'const.product.model').strip() == 'emulator'
assert guest('param', 'get', 'const.product.devicetype').strip() == '2in1'
instances = json.loads(run(emulator, '-list', '-details'))
instance = next(x for x in instances if x['name'] == args.instance)
assert instance['deviceType'] == '2in1' and instance['isRunning'] == 'true'
assert [x['name'] for x in instances if x['isRunning'] == 'true'] == [args.instance], \
    'Run only this isolated emulator so the named CLI guest and loopback target match'
app_pid = int(guest('pidof', 'dev.nativeapi.smoke').strip())
# Window gestures start on an activated app, as with a normal desktop harness.
# A system picker may restore a previously focused Settings/browser window.
assert 'start ability successfully' in guest('aa', 'start', '-a', 'EntryAbility', '-b', 'dev.nativeapi.smoke')
assert int(guest('pidof', 'dev.nativeapi.smoke').strip()) == app_pid, 'Activation must preserve the completed fixture process'

# The identity checks are read-only; the UI tree below must additionally
# contain our rendered PASS report before any gesture is permitted.


def windows(label):
    dump = guest('hidumper', '-s', 'WindowManagerService', '-a', '-a')
    (out / f'{label}-windows.txt').write_text(dump)
    rows = []
    for line in dump.splitlines():
        match = re.match(r'^(\S+)\s+(\d+)\s+(\d+)\s+(\d+)\s+(\d+)\s+(\d+)\s+'
                         r'(\d+)\s+(-?\d+)\s+(\d+)\s+\[\s*(-?\d+)\s+(-?\d+)\s+'
                         r'(\d+)\s+(\d+)\s*\]', line)
        if match:
            values = list(map(int, match.groups()[1:]))
            rows.append(dict(pid=values[1], id=values[2], z=values[6],
                             x=values[8], y=values[9], w=values[10], h=values[11]))
    own = [x for x in rows if x['pid'] == app_pid and x['z'] > 0]
    assert len(own) == 1, f'Expected one visible test window, got {own}'
    return own[0], rows


def widget(label, pattern):
    run(emulator, '-instance', args.instance, '-uiLayout', '-i')
    text = (Path(instance['instancePath']) / 'uiLayout/analysis.md').read_text()
    (out / f'{label}-layout.md').write_text(text)
    assert re.search(r'NATIVEAPI_OHOS_PASS \(\d+ passed, 0 failed\)', text), \
        'Guest must show our passing fixture'
    found = re.search(pattern + r' \[id:(\d+)\] \[top: (\d+), left: (\d+), width: (\d+), height: (\d+)\]', text)
    assert found, f'Widget missing: {pattern}'
    widget_id, top, left, width, height = map(int, found.groups())
    assert width > 0 and height > 0
    return widget_id, left + width // 2, top + height // 2


def guard(label, x, y):
    own, _ = windows(label)
    # The compositor exposes full-screen transparent system layers in its window
    # list. UITest reports visible window roots with the occluded areas clipped,
    # so use that native ownership data instead of treating every layer as input.
    remote_layout = '/data/local/tmp/nativeapi-desktop-ui-layout.json'
    guest('uitest', 'dumpLayout', '-p', remote_layout)
    local_layout = out / f'{label}-ownership.json'
    run(hdc, '-t', args.device, 'file', 'recv', remote_layout, local_layout)
    roots = json.loads(local_layout.read_text())['children']
    hits = []
    for root in roots:
        attributes = root['attributes']
        rect = re.fullmatch(r'\[(-?\d+),(-?\d+)\]\[(-?\d+),(-?\d+)\]', attributes['bounds'])
        assert rect, 'Invalid native ownership rectangle'
        left, top, right, bottom = map(int, rect.groups())
        if left <= x < right and top <= y < bottom:
            hits.append(attributes)
    assert len(hits) == 1 and hits[0].get('bundleName') == 'dev.nativeapi.smoke' and \
        hits[0].get('hostWindowId') == str(own['id']), 'Press point is not owned by the test app'
    return own


def settle(label, condition):
    for attempt in range(20):
        own, _ = windows(label)
        if condition(own):
            return own
        time.sleep(0.15)
    raise AssertionError(f'{label}: native geometry did not reach expected state: {own}')


identifier, x, y = widget('before-drag', r'Text NativeAPI Desktop Acceptance')
before = guard('before-drag', x, y)
# Target stays inside the isolated guest display. No host cursor or keys are used.
run(emulator, '-instance', args.instance, '-slide', f'{identifier} {x + 240} {y + 160}')
dragged = settle('after-drag', lambda r: abs(r['x'] - before['x'] - 240) <= 8 and
                 abs(r['y'] - before['y'] - 160) <= 8 and r['w'] == before['w'] and r['h'] == before['h'])
print('PASS guest title-bar drag moves the actual desktop window')
identifier, x, y = widget('before-maximize', r'Button EnhanceMaximizeBtn')
guard('before-maximize', x, y)
run(emulator, '-instance', args.instance, '-click', identifier)
settle('after-maximize', lambda r: r['w'] > dragged['w'] + 100 and r['h'] > dragged['h'] + 100)
print('PASS guest maximize control expands actual native geometry')
identifier, x, y = widget('before-restore', r'Button Enhance(?:Maximize|Restore|Recover)Btn')
guard('before-restore', x, y)
run(emulator, '-instance', args.instance, '-click', identifier)
settle('after-restore', lambda r: all(abs(r[k] - dragged[k]) <= 2 for k in ('x', 'y', 'w', 'h')))
print('PASS guest restore control returns to the prior floating rectangle')
print('NATIVEAPI_OHOS_DESKTOP_UI_PASS (3 passed, 0 failed)')
