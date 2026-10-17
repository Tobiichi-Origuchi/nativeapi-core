#!/usr/bin/env python3
"""Drive native views inside a named disposable guest; never inject host input."""
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
out = Path(__file__).resolve().parent / 'test-results/view-ui' / str(args.pid)
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
    result = run(emulator, '-instance', instance['name'], '-uiLayout')
    assert 'Scenario simulation success' in result, result
    text = (Path(instance['instancePath']) / 'uiLayout/analysis.md').read_text(encoding='utf-8')
    (out / f'{phase}-layout.md').write_text(text, encoding='utf-8')
    return text


def wait_phase(phase, expected):
    for _ in range(90):
        assert int(guest('pidof', 'dev.nativeapi.smoke').strip()) == args.pid
        log = guest('hilog', '-x', '-T', 'nativeapi-view', '-P', args.pid)
        assert not re.search(r'\bFAIL\b|NATIVEAPI_VIEW_FAIL', log), log[-8000:]
        if f'NATIVEAPI_VIEW_UI_READY {phase}' in log:
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
    remote = '/data/local/tmp/nativeapi-view-ownership.json'
    guest('uitest', 'dumpLayout', '-p', remote)
    target = out / f'{phase}-ownership.json'
    run(hdc, '-t', args.device, 'file', 'recv', remote, target)
    hits = []
    for root in json.loads(target.read_text(encoding='utf-8'))['children']:
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


text = wait_phase('first', 'Button NativeAPI View Step 1 ')
assert 'Text NativeAPI Views Ready 中文 🙂 ' in text and 'TextInput NativeAPI prefilled 中文 🙂 ' in text
assert 'NativeAPI Must Be Hidden' not in text
assert 'NativeAPI secret' not in text and re.search(r'TextInput \*{5,} ', text)
images = re.findall(r'Image \[top: (\d+), left: (\d+), width: (\d+), height: (\d+)\]', text)
pictures = [r for r in images if int(r[2]) >= 96 and 1.45 < int(r[2]) / int(r[3]) < 1.55]
assert len(pictures) == 1, images
picture_dimensions = f'width: {pictures[0][2]}, height: {pictures[0][3]}]'
run(emulator, '-instance', instance['name'], '-screenshot', '-screenshotPath', out)
click('disabled', r'Button NativeAPI Disabled')
click('first', r'Button NativeAPI View Step 1')
print('PASS native label/input/image visible, hidden label absent, guarded disabled and enabled clicks', flush=True)
text = wait_phase('second', 'Button NativeAPI View Step 2 ')
assert 'Text NativeAPI Reparented 中文 ' in text and 'TextArea NativeAPI multiline' in text
assert not re.search(r'Image \[top: \d+, left: \d+, ' + re.escape(picture_dimensions), text)
click('second', r'Button NativeAPI View Step 2')
print('PASS native reparenting, multiline replacement and image hiding', flush=True)
wait_phase('c', 'Button NativeAPI C Step ')
click('c', r'Button NativeAPI C Step')
for _ in range(90):
    log = guest('hilog', '-x', '-T', 'nativeapi-view', '-P', args.pid)
    assert not re.search(r'\bFAIL\b|NATIVEAPI_VIEW_FAIL', log), log[-8000:]
    if 'NATIVEAPI_VIEW_PASS' in log:
        text = layout('finished')
        assert 'Text NativeAPI Views PASS ' in text and 'Button NativeAPI ' not in text
        (out / 'native.log').write_text(log, encoding='utf-8')
        print('PASS C ABI click and native tree cleanup', flush=True)
        print('NATIVEAPI_VIEW_UI_PASS (3 passed, 0 failed)', flush=True)
        break
    time.sleep(0.25)
else:
    raise AssertionError('Native view lifecycle tests did not finish')
