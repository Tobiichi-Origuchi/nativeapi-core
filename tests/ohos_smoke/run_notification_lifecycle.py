#!/usr/bin/env python3
"""Prove notification retention and cold body/button activation in a real guest."""
import argparse
import json
import re
import subprocess
import sys
import time
from pathlib import Path

p = argparse.ArgumentParser()
p.add_argument('deveco', type=Path)
p.add_argument('--device', default='127.0.0.1:5555')
a = p.parse_args()
assert re.fullmatch(r'127\.0\.0\.1:\d+', a.device)
root = Path(__file__).resolve().parent
hdc = a.deveco / 'sdk/default/openharmony/toolchains/hdc'
emulator = a.deveco / 'tools/emulator/Emulator'


def run(*cmd):
    return subprocess.check_output([str(c) for c in cmd], stdin=subprocess.DEVNULL,
                                   text=True, encoding='utf-8', stderr=subprocess.STDOUT, timeout=60)


def guest(*cmd):
    return run(hdc, '-t', a.device, 'shell', *cmd)


assert guest('param', 'get', 'const.product.model').strip() == 'emulator'
kind = guest('param', 'get', 'const.product.devicetype').strip()
instances = [i for i in json.loads(run(emulator, '-list', '-details')) if i['isRunning'] == 'true']
assert len(instances) == 1 and instances[0]['name'].startswith('NativeAPI_')
assert kind in ('phone', '2in1') and instances[0]['deviceType'] == kind
out = root / 'test-results/notification-lifecycle' / f'{kind}-{time.time_ns()}'
out.mkdir(parents=True)
manifest = []


def stamp():
    value = guest("date '+%m-%d %H:%M:%S'").strip()
    assert re.fullmatch(r'\d\d-\d\d \d\d:\d\d:\d\d', value), value
    return value


def wait(pid, mode, after):
    deadline = time.monotonic() + 100
    while time.monotonic() < deadline:
        assert guest('pidof', 'dev.nativeapi.smoke').strip() == str(pid), 'Fixture process changed'
        raw = guest('hilog', '-x', '-T', 'nativeapi-notification', '-P', pid)
        text = '\n'.join(line for line in raw.splitlines() if line[:14] >= after)
        (out / f'{pid}-{mode}.log').write_text(text, encoding='utf-8')
        assert not re.search(r'nativeapi-notification:.*\bFAIL ', text), text
        begin = text.rfind('NATIVEAPI_NOTIFICATION_MODE ' + mode)
        matches = re.findall(r'NATIVEAPI_NOTIFICATION_PASS \((\d+) passed, 0 failed\)', text[begin:]) if begin >= 0 else []
        if matches:
            return int(matches[-1])
        if mode.startswith('prepare-') and 'NATIVEAPI_NOTIFICATION_AUTHORIZING' in text:
            run(sys.executable, root / 'notification_ui.py', a.deveco,
                '--device', a.device, '--pid', pid, '--action', 'authorize')
        time.sleep(.3)
    raise AssertionError(f'{mode} did not complete in PID {pid}')


def cold_ui(pid, tag, action):
    result = run(sys.executable, root / 'notification_ui.py', a.deveco,
                 '--device', a.device, '--pid', pid, '--cold', '--tag', tag, '--action', action)
    (out / f'{pid}-{action}-ui.log').write_text(result, encoding='utf-8')


for action in ['open', 'button']:
    before = stamp()
    guest('aa', 'force-stop', 'dev.nativeapi.smoke')
    status = guest('aa', 'start', '-a', 'EntryAbility', '-b', 'dev.nativeapi.smoke',
                   '--ps', 'nativeapiSuite', 'notification-prepare-' + action)
    assert 'start ability successfully' in status, status
    time.sleep(.5)
    old_pid = int(guest('pidof', 'dev.nativeapi.smoke').strip())
    prepared = wait(old_pid, 'prepare-' + action, before)
    metadata = guest('bm', 'dump', '-n', 'dev.nativeapi.smoke')
    installed = json.loads(metadata[metadata.index('{'):])
    uid = installed['applicationInfo']['uid']
    command = guest('cat', f'/proc/{old_pid}/cmdline').split('\0')[0]
    status = guest('cat', f'/proc/{old_pid}/status')
    match = re.search(r'^Uid:\s+(\d+)\s+(\d+)\s+(\d+)\s+(\d+)', status, re.M)
    assert command == 'dev.nativeapi.smoke' and match and all(int(x) == uid for x in match.groups())
    (out / f'{old_pid}-identity.json').write_text(json.dumps(dict(pid=old_pid, uid=uid, command=command)), encoding='utf-8')
    guest('aa', 'force-stop', 'dev.nativeapi.smoke')
    for _ in range(40):
        if not guest('pidof', 'dev.nativeapi.smoke').strip():
            break
        time.sleep(.1)
    else:
        raise AssertionError('Old app process did not stop; no cold-launch claim is possible')
    tag = 'cold_' + action
    cold_ui(old_pid, tag, 'panel')
    if action == 'button':
        cold_ui(old_pid, tag, 'expand')
    clicked_after = stamp()
    cold_ui(old_pid, tag, 'button' if action == 'button' else 'body')
    for _ in range(100):
        new_pid = guest('pidof', 'dev.nativeapi.smoke').strip()
        if new_pid:
            break
        time.sleep(.1)
    else:
        raise AssertionError('Real notification click did not launch the stopped app')
    new_pid = int(new_pid)
    assert new_pid != old_pid, 'Cold activation must use a fresh process'
    recovered = wait(new_pid, 'recover-' + action, clicked_after)
    manifest.append(dict(action=action, old_pid=old_pid, new_pid=new_pid,
                         prepared_checks=prepared, recovery_checks=recovered, after=before))
    (out / 'cases.json').write_text(json.dumps(manifest, indent=2), encoding='utf-8')
    print(f'PASS {kind} real cold {action}: stopped PID {old_pid}, notification launched PID {new_pid}, {prepared}+{recovered} checks', flush=True)
print('NATIVEAPI_NOTIFICATION_COLD_SUITE_PASS (2 real cold launches)')
(root / f'test-results/notification-cold-{kind}-evidence.txt').write_text(str(out)+'\n', encoding='utf-8')
