#!/usr/bin/env python3
"""Run approved and required Ability termination in separate fresh fixture processes."""
import argparse
import json
import os
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
# Fail before modifying the fixture if the selected test interpreter lacks PNG support.
import PIL.Image  # noqa: E402,F401
suffix = '.exe' if os.name == 'nt' else ''
hdc = a.deveco / f'sdk/default/openharmony/toolchains/hdc{suffix}'
emulator = a.deveco / f'tools/emulator/Emulator{suffix}'
root = Path(__file__).resolve().parent

def run(*cmd, timeout=40):
    return subprocess.check_output([str(x) for x in cmd], stdin=subprocess.DEVNULL,
                                   text=True, encoding='utf-8', stderr=subprocess.STDOUT, timeout=timeout)

def guest(*cmd):
    return run(hdc, '-t', a.device, 'shell', *cmd)

assert guest('param', 'get', 'const.product.model').strip() == 'emulator'
kind = guest('param', 'get', 'const.product.devicetype').strip()
instances = [i for i in json.loads(run(emulator, '-list', '-details')) if i['isRunning'] == 'true']
assert len(instances) == 1 and instances[0]['name'] == ('NativeAPI_Phone' if kind == 'phone' else 'NativeAPI_Desktop')
assert kind == instances[0]['deviceType']
out = root / 'test-results/application-suite' / f'{kind}-{time.time_ns()}'
out.mkdir(parents=True)
cases = []
for action in ['full', 'required']:
    guest('aa', 'force-stop', 'dev.nativeapi.smoke')
    for _ in range(50):
        if not guest('pidof', 'dev.nativeapi.smoke').strip():
            break
        time.sleep(.1)
    else:
        raise AssertionError('The previous fixture did not stop; refusing to reuse old lifecycle evidence')
    result = guest('aa', 'start', '-a', 'EntryAbility', '-b', 'dev.nativeapi.smoke', '--ps', 'nativeapiSuite', 'application')
    assert 'start ability successfully' in result, result
    pid = ''
    for _ in range(50):
        pid = guest('pidof', 'dev.nativeapi.smoke').strip()
        if re.fullmatch(r'\d+', pid):
            break
        time.sleep(.1)
    assert re.fullmatch(r'\d+', pid)
    if kind == 'phone' and action == 'full':
        def await_native(pattern):
            deadline = time.monotonic() + 30
            while time.monotonic() < deadline:
                assert guest('pidof', 'dev.nativeapi.smoke').strip() == pid
                records = guest('hilog', '-x', '-P', pid, '-T', 'nativeapi-app')
                assert 'FAIL' not in records, records
                if re.search(pattern, records):
                    return records
                time.sleep(.2)
            raise AssertionError('Actual Ability lifecycle callback did not arrive: ' + pattern)
        await_native('NATIVEAPI_APP_BASE_PASS')
        metadata = guest('bm', 'dump', '-n', 'com.huawei.hmos.settings')
        settings = json.loads(metadata[metadata.index('{'):])
        modules = [m for m in settings['hapModuleInfos'] if m.get('moduleName', m.get('name')) == settings['mainEntry']]
        assert len(modules) == 1 and modules[0]['mainElementName']
        guest('aa', 'start', '-a', modules[0]['mainElementName'], '-b', 'com.huawei.hmos.settings')
        records = await_native(r'ApplicationDeactivatedEvent running=1 visible=0')
        (out / 'phone-background.log').write_text(records)
        remote_tree = '/data/local/tmp/nativeapi-application-background.json'
        assert 'DumpLayout saved' in guest('uitest', 'dumpLayout', '-p', remote_tree)
        tree_path = out / 'phone-background-tree.json'
        run(hdc, '-t', a.device, 'file', 'recv', remote_tree, tree_path)
        tree = json.loads(tree_path.read_text())
        bundles = [r['attributes'].get('bundleName') for r in tree['children']]
        assert 'com.huawei.hmos.settings' in bundles and 'dev.nativeapi.smoke' not in bundles, bundles
        guest('aa', 'start', '-a', 'EntryAbility', '-b', 'dev.nativeapi.smoke', '--ps', 'nativeapiSuite', 'application')
        deadline = time.monotonic() + 30
        while time.monotonic() < deadline:
            records = guest('hilog', '-x', '-P', pid, '-T', 'nativeapi-app')
            assert 'FAIL' not in records, records
            if len(re.findall(r'ApplicationActivatedEvent running=1 visible=1', records)) >= 2:
                break
            time.sleep(.2)
        else:
            raise AssertionError('Original phone Ability did not return to foreground')
        assert guest('pidof', 'dev.nativeapi.smoke').strip() == pid
        (out / 'phone-foreground.log').write_text(records)
        print('PASS actual phone background/foreground transitions preserve the same Ability process', flush=True)
    text = run(sys.executable, root / 'verify_application_ui.py', a.deveco, '--device', a.device,
               '--pid', pid, '--action', action, timeout=240)
    (out / f'{action}.log').write_text(text)
    native = (root / 'test-results/application-ui' / pid / 'native.log').read_text()
    match = re.search(r'NATIVEAPI_APP_FINAL_PASS passed=(\d+) failed=0', native)
    assert match and 'FAIL' not in native
    cases.append(dict(action=action, pid=int(pid), native_checks=int(match[1]), guarded_actions=6 if action=='full' else 1))
    (out / 'cases.json').write_text(json.dumps(cases, indent=2))
    print(text, flush=True)
print('NATIVEAPI_APPLICATION_SUITE_PASS ' + json.dumps(cases), flush=True)
