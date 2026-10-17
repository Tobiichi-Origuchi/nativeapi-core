#!/usr/bin/env python3
"""Read-only acceptance of real accessibility state inside a disposable guest."""
import argparse
import json
from pathlib import Path
import re
import subprocess
import time

parser = argparse.ArgumentParser()
parser.add_argument('deveco')
parser.add_argument('--device', default='127.0.0.1:5555')
args = parser.parse_args()
assert re.fullmatch(r'127\.0\.0\.1:\d+', args.device), 'Disposable local emulator required'
root = Path(args.deveco)
hdc = root / 'sdk/default/openharmony/toolchains' / ('hdc.exe' if (root / 'sdk/default/openharmony/toolchains/hdc.exe').exists() else 'hdc')

def run(*cmd):
    return subprocess.run([str(x) for x in cmd], check=True, stdin=subprocess.DEVNULL,
                          stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, timeout=25).stdout

def guest(*cmd):
    return run(hdc, '-t', args.device, 'shell', *cmd)

assert 'emulator' in guest('param', 'get', 'const.product.model')
kind = guest('param', 'get', 'const.product.devicetype').strip()
assert kind in ('phone', '2in1')
emulator = root / 'tools/emulator' / ('Emulator.exe' if (root / 'tools/emulator/Emulator.exe').exists() else 'Emulator')
instances = [i for i in json.loads(run(emulator, '-list', '-details')) if i['isRunning'] == 'true']
assert len(instances) == 1 and instances[0]['name'] == ('NativeAPI_Phone' if kind == 'phone' else 'NativeAPI_Desktop')
assert instances[0]['deviceType'] == kind
run_id = str(time.time_ns())
print(guest('aa', 'start', '-a', 'EntryAbility', '-b', 'dev.nativeapi.smoke', '--ps', 'nativeapiSuite', 'accessibility', '--ps', 'nativeapiAccessRun', run_id), flush=True)
process = ''
for _ in range(30):
    process = guest('pidof', 'dev.nativeapi.smoke').strip()
    if re.fullmatch(r'\d+', process):
        break
    time.sleep(.2)
assert re.fullmatch(r'\d+', process), 'Fixture did not start'
assert guest('cat', f'/proc/{process}/cmdline').split('\0')[0] == 'dev.nativeapi.smoke'
out = Path(__file__).resolve().parent / 'test-results/accessibility' / process / run_id
out.mkdir(parents=True, exist_ok=True)
log = ''
for _ in range(100):
    assert guest('pidof', 'dev.nativeapi.smoke').strip() == process, 'Fixture terminated or restarted'
    log = guest('hilog', '-x', '-P', process, '-T', 'nativeapi-access')
    begin = log.find('NATIVEAPI_ACCESSIBILITY_RUN ' + run_id)
    log = log[begin:] if begin >= 0 else ''
    (out / 'runtime.log').write_text(log)
    assert 'NATIVEAPI_ACCESSIBILITY_FAIL' not in log, log
    if 'NATIVEAPI_ACCESSIBILITY_PASS' in log:
        break
    time.sleep(.3)
assert 'NATIVEAPI_ACCESSIBILITY_PASS (12 passed, 0 failed)' in log, log
reference = re.search(r'SDK_REFERENCE enabled=([01]) enabled_extensions=(\d+)', log)
assert reference, log
system_state = guest('hidumper', '-s', 'AccessibilityManagerService', '-a', '-u')
(out / 'system-state.txt').write_text(system_state)
observed = re.search(r'^accessible:\s+([01])$', system_state, re.M)
assert observed and observed[1] == reference[1], system_state
print('PASS actual system accessibility service state agrees with SDK and native APIs')
(out / 'sdk.log').write_text(guest('hilog', '-x', '-P', process, '-T', 'JSAPP'))
(out / 'process.txt').write_text(guest('cat', f'/proc/{process}/status'))
(out / 'bundle.txt').write_text(guest('bm', 'dump', '-n', 'dev.nativeapi.smoke'))
print(log)
print(f'PASS accessibility fixture PID {process}; evidence {out}', flush=True)
