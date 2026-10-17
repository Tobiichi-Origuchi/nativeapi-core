#!/usr/bin/env python3
"""Real notification authorization, system records, action and body acceptance."""
import argparse
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


def run(*cmd):
    return subprocess.check_output([str(c) for c in cmd], stdin=subprocess.DEVNULL, text=True,
                                   encoding='utf-8', stderr=subprocess.STDOUT, timeout=50)


def guest(*cmd):
    return run(hdc, '-t', a.device, 'shell', *cmd)


assert guest('param', 'get', 'const.product.model').strip() == 'emulator'
kind = guest('param', 'get', 'const.product.devicetype').strip()
assert kind in ('phone', '2in1')
guest('aa', 'force-stop', 'dev.nativeapi.smoke')
assert 'start ability successfully' in guest('aa', 'start', '-a', 'EntryAbility', '-b',
                                            'dev.nativeapi.smoke', '--ps', 'nativeapiSuite', 'notification')
time.sleep(1)
pid = int(guest('pidof', 'dev.nativeapi.smoke').strip())
out = root / 'test-results/notification-ui' / str(pid)
out.mkdir(parents=True, exist_ok=True)


def log():
    assert int(guest('pidof', 'dev.nativeapi.smoke').strip()) == pid
    text = guest('hilog', '-x', '-T', 'nativeapi-notification', '-P', pid)
    (out / 'runtime.log').write_text(text, encoding='utf-8')
    assert 'NATIVEAPI_NOTIFICATION_FAIL' not in text and not re.search(r'nativeapi-notification:.*\bFAIL ', text), text
    return text


def ui(action):
    try:
        result = run(sys.executable, root / 'notification_ui.py', a.deveco,
                     '--device', a.device, '--pid', pid, '--action', action)
    except subprocess.CalledProcessError as e:
        print(e.output, flush=True)
        raise
    (out / f'{action}.log').write_text(result, encoding='utf-8')
    print(f'PASS guarded notification interaction: {action}', flush=True)


def wait(marker, authorize=False):
    deadline = time.monotonic() + 90
    while time.monotonic() < deadline:
        text = log()
        if marker in text:
            return text
        if authorize and 'NATIVEAPI_NOTIFICATION_AUTHORIZING' in text:
            ui('authorize')
        time.sleep(.3)
    raise AssertionError(f'{marker} not reached in PID {pid}')


wait('NATIVEAPI_NOTIFICATION_BUTTON_READY', authorize=True)
ui('panel')
ui('expand')
ui('button')
wait('NATIVEAPI_NOTIFICATION_BODY_READY')
ui('panel')
ui('body')
text = wait('NATIVEAPI_NOTIFICATION_PASS')
match = re.search(r'NATIVEAPI_NOTIFICATION_PASS \((\d+) passed, 0 failed\)', text)
assert match
(root / f'test-results/notification-{kind}-pid.txt').write_text(str(pid)+'\n', encoding='utf-8')
print(f'NATIVEAPI_NOTIFICATION_SUITE_PASS ({match[1]} native checks, real button and body, PID {pid})')
