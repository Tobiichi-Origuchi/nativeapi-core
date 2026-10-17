#!/usr/bin/env python3
"""Drive native dialogs inside a named disposable guest; never inject host input."""
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
out = Path(__file__).resolve().parent / 'test-results/dialog-ui'
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


def layout(phase):
    run(emulator, '-instance', instance['name'], '-uiLayout', '-i')
    text = (Path(instance['instancePath']) / 'uiLayout/analysis.md').read_text(encoding='utf-8')
    (out / f'{phase}-layout.md').write_text(text, encoding='utf-8')
    return text


def wait_phase(phase, title):
    for _ in range(60):
        assert int(guest('pidof', 'dev.nativeapi.smoke').strip()) == args.pid
        log = guest('hilog', '-x', '-T', 'nativeapi-test', '-P', args.pid)
        assert 'NATIVEAPI_OHOS_FAIL' not in log, log[-2000:]
        if f'NATIVEAPI_DIALOG_UI_READY {phase}' in log:
            text = layout(phase)
            if '是否将应用自动添加至桌面？' in text and 'NativeAPI Smoke' in text:
                # A pending system installation prompt can cover the fixture's
                # first native dialog. The helper verifies the own app label,
                # SceneBoard window owner and unobstructed button before clicking.
                run('python3', Path(__file__).with_name('notification_ui.py'), args.deveco,
                    '--device', args.device, '--pid', args.pid, '--action', 'dismiss-shortcut')
                continue
            if f'Text NativeAPI Dialog {title} ' in text:
                return text
        time.sleep(0.25)
    raise AssertionError(f'Native dialog did not reach {phase}')


def click(phase, pattern):
    text = layout(phase)
    assert 'Text NativeAPI Dialog ' in text
    matches = re.findall(pattern + r' \[id:(\d+)\] \[top: (\d+), left: (\d+), width: (\d+), height: (\d+)\]', text)
    assert len(matches) == 1, f'Expected one {pattern}: {text}'
    identifier, top, left, width, height = map(int, matches[0])
    assert width > 0 and height > 0
    x, y = left + width // 2, top + height // 2
    # Confirm native ownership immediately before each press, including overlays.
    assert int(guest('pidof', 'dev.nativeapi.smoke').strip()) == args.pid
    remote = '/data/local/tmp/nativeapi-dialog-ownership.json'
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
    run(emulator, '-instance', instance['name'], '-click', identifier)
    time.sleep(0.35)


text = wait_phase('primary', 'Primary')
assert 'TextInput 预填 NativeAPI 🙂 ' in text and 'Progress 25.000000 ' in text
assert 'Text NativeAPI remember ' in text
click('checkbox', r'Checkbox')
click('primary', r'Button Accept NativeAPI')
print('PASS native dialog displays prefilled input/progress and accepts checkbox/primary clicks', flush=True)
wait_phase('secondary', 'Secondary')
click('secondary', r'Button Other NativeAPI')
print('PASS native secondary button is clickable', flush=True)
text = wait_phase('close', 'Close')
assert 'TextInput ' not in text and 'Checkbox ' not in text and 'Progress ' not in text
click('close', r'Button Dismiss NativeAPI')
print('PASS hidden optional controls and native close button', flush=True)
wait_phase('modal', 'Modal')
click('modal', r'Button Accept NativeAPI')
print('PASS native window-modal dialog accepts primary click', flush=True)
for _ in range(80):
    log = guest('hilog', '-x', '-T', 'nativeapi-test', '-P', args.pid)
    assert 'NATIVEAPI_OHOS_FAIL' not in log, log[-4000:]
    if 'NATIVEAPI_OHOS_PASS' in log:
        text = layout('after-cleanup')
        assert 'Text NativeAPI Dialog ' not in text and re.search(r'NATIVEAPI_OHOS_PASS \(\d+ passed, 0 failed\)', text)
        print('PASS all native dialogs disappear after closure, parent destruction and disposal', flush=True)
        break
    time.sleep(0.25)
else:
    raise AssertionError('Native lifecycle tests did not finish')
print('NATIVEAPI_OHOS_DIALOG_UI_PASS (5 passed, 0 failed)', flush=True)
