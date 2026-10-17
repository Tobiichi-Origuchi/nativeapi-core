#!/usr/bin/env python3
"""Compare actual pointer locations with click events, the SDK, C++ and C ABI."""
import argparse
import json
import math
import re
import time
from pathlib import Path
from ohos_ui_fixture import Fixture

p = argparse.ArgumentParser()
p.add_argument('deveco', type=Path)
p.add_argument('--device', default='127.0.0.1:5555')
p.add_argument('--expect-pointer', choices=['present', 'absent'], required=True)
a = p.parse_args()
f = Fixture(a.deveco, a.device, 'cursor', 'CURSOR')
f.wait('NATIVEAPI_CURSOR_READY')
checks = 0
seen = {}


def check(value, message):
    global checks
    assert value, message
    checks += 1


def owner(identifier, label):
    tree = f.tree(label)
    matches = [(root, node) for root in tree['children']
               if root['attributes'].get('bundleName') == 'dev.nativeapi.smoke'
               for node in f.walk(root) if node['attributes'].get('id') == identifier]
    assert len(matches) == 1
    root, node = matches[0]
    l, t, r, b = f.rect(node)
    x, y = (l + r) // 2, (t + b) // 2
    assert r > l and b > t and node['attributes'].get('enabled') == 'true'
    hits = [n for n in tree['children'] if f.rect(n)[0] <= x < f.rect(n)[2]
            and f.rect(n)[1] <= y < f.rect(n)[3]]
    assert len(hits) == 1 and hits[0]['attributes'] == root['attributes']
    wid = int(root['attributes']['hostWindowId'])
    row = f.windows(label)[wid]
    assert row[2] == f.pid and int(row[7]) > 0
    f.identity()
    (f.out / (label + '-owner.json')).write_text(json.dumps(
        dict(point=[x, y], window=wid, pid=f.pid, uid=f.uid, node=node['attributes'])))
    return x, y


def press(identifier, label):
    x, y = owner(identifier, label + '-before')
    if f.kind == '2in1' or a.expect_pointer == 'present':
        result = f.guest('uinput', '-M', '-m', x, y, x, y, 400, '--trace')
        assert not re.search(r'failed|invalid|error', result, re.I), result
        # A fresh owner snapshot after the approach, before the atomic press.
        assert owner(identifier, label + '-press') == (x, y)
        query = f.guest('uinput', '-M', '-q')
        (f.out / (label + '-mouse.txt')).write_text(query)
        result = f.guest('uinput', '-M', '-m', x, y, '-c', 0)
        assert not re.search(r'failed|invalid|error', result, re.I), result
    else:
        f.click(identifier, label + '-touch')
    return [x, y]


def sample(phase):
    marker = 'NATIVEAPI_CURSOR_SAMPLE ' + phase + ' '
    deadline = time.monotonic() + 35
    while True:
        text = f.logs()
        rows = [line.split(marker, 1)[1] for line in text.splitlines() if marker in line]
        if len(rows) > seen.get(phase, 0):
            seen[phase] = len(rows)
            break
        assert time.monotonic() < deadline, ('Missing fresh sample', phase)
        time.sleep(.1)
    row = rows[-1]
    state = json.loads(row)
    (f.out / (phase + '-sample.json')).write_text(json.dumps(state, indent=2))
    check(state['code'] == state['workerCode'] == state['afterCode'], 'SDK result changed during sample')
    if state['code'] == 0:
        check(state['primary'], 'This fixture requires a real primary-screen pointer')
        check(state['display'] == state['workerDisplay'] == state['afterDisplay'], 'Pointer display changed')
        check(state['sdk'] == state['workerSdk'] == state['after'], 'Pointer moved during sample')
        expected = [v / state['scale'] for v in state['sdk']]
    else:
        expected = [0, 0]
    for key in ['cpp', 'c', 'workerCpp', 'workerC']:
        check(all(math.isfinite(v) and abs(v - target) < 1e-6
                  for v, target in zip(state[key], expected)), (phase, key, state))
    return state


positions = []
for phase in ['a', 'b', 'c']:
    physical = press('nativeapi-cursor-' + phase, phase)
    state = sample(phase)
    check(state['focused'], 'Foreground window is not actually focused')
    check(state['code'] == (0 if a.expect_pointer == 'present' else 3900010),
          ('Unexpected foreground SDK result', state))
    check(all(abs(v * state['scale'] - target) <= 2
              for v, target in zip(state['event'], physical)),
          ('Click was not delivered at the guarded point', state, physical))
    if a.expect_pointer == 'present':
        check(state['code'] == 0, ('Desktop pointer query failed', state))
        check(all(abs(v - target) <= 1 for v, target in zip(state['sdk'], physical)),
              ('Pointer differs from actual guarded press', state, physical))
        check(all(abs(v - target) < 1 for v, target in zip(state['cpp'], state['event'])),
              ('Pointer differs from independently delivered ArkUI click coordinates', state))
        positions.append(state['cpp'])
    print('PASS actual pointer query', phase, 'SDK code', state['code'], flush=True)
if positions:
    check(len({tuple(p) for p in positions}) == 3 and all(min(p) > 0 for p in positions),
          'Three different nonzero real pointer positions required')
f.screenshot('foreground')
press('nativeapi-cursor-blur', 'blur')
unfocused = sample('unfocused')
check(not unfocused['focused'], 'Fixture window did not actually lose focus')
check(unfocused['code'] == (3900009 if a.expect_pointer == 'present' else 3900010),
      ('Unexpected SDK result after actual focus loss', unfocused))
f.windows('unfocused')
f.wait('NATIVEAPI_CURSOR_RESTORED')
time.sleep(.5)
press('nativeapi-cursor-a', 'restored-a')
restored = sample('a')
check(restored['focused'], 'Window did not regain focus')
check(restored['code'] == (0 if a.expect_pointer == 'present' else 3900010),
      'Unexpected pointer result after foreground restoration')
f.screenshot('restored')
(f.out / 'result.json').write_text(json.dumps(dict(pid=f.pid, kind=f.kind, checks=checks, pointer=a.expect_pointer)))
print(f'NATIVEAPI_CURSOR_{a.expect_pointer.upper()}_PASS ({checks} checks); PID {f.pid}; evidence {f.out}', flush=True)
