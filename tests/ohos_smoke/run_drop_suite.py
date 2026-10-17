#!/usr/bin/env python3
"""Owned-guest touch drags from a real ArkTS SDK source into a native destination."""
import argparse
import json
import re
import time
from pathlib import Path
from ohos_ui_fixture import Fixture


def main():
    p = argparse.ArgumentParser()
    p.add_argument('deveco', type=Path)
    p.add_argument('--device', default='127.0.0.1:5555')
    a = p.parse_args()
    f = Fixture(a.deveco, a.device, 'drop-target', 'DROP')
    f.wait('NATIVEAPI_DROP_READY')
    commands = 0
    gestures = 0

    def command(name):
        nonlocal commands
        commands += 1
        nonce = f'drop-{time.time_ns()}'
        f.guest('aa', 'start', '-a', 'EntryAbility', '-b', 'dev.nativeapi.smoke',
                '--ps', 'nativeapiSuite', 'drop-target', '--ps', 'nativeapiDropCommand', name,
                '--ps', 'nativeapiDropRequest', nonce)
        marker = f'NATIVEAPI_DROP_REPLY {{"request":"{nonce}"'
        records = f.wait(marker)
        result = json.loads(next(line.split('NATIVEAPI_DROP_REPLY ', 1)[1]
                                 for line in records.splitlines() if marker in line))
        assert result['failed'] == 0, result
        return result

    def drag(label, leave=False):
        nonlocal gestures
        tree = f.tree(label)
        nodes = {}
        for root in tree['children']:
            if root['attributes'].get('bundleName') == 'dev.nativeapi.smoke':
                for node in f.walk(root):
                    key = node['attributes'].get('id')
                    if key in ['nativeapi-drop-source', 'nativeapi-drop-target']:
                        assert key not in nodes
                        nodes[key] = node
        assert len(nodes) == 2, nodes
        points = []
        for key in ['nativeapi-drop-source', 'nativeapi-drop-target']:
            l, t, r, b = f.rect(nodes[key])
            assert r > l and b > t
            points.append(((l + r) // 2, b + 8 if leave and key == 'nativeapi-drop-target' else (t + b) // 2))
        # Check the whole straight gesture against a fresh native ownership tree.
        fresh = f.tree(label + '-fresh')
        rows = f.windows(label)
        for i in range(21):
            x = points[0][0] + (points[1][0] - points[0][0]) * i // 20
            y = points[0][1] + (points[1][1] - points[0][1]) * i // 20
            hits = [r for r in fresh['children'] if f.rect(r)[0] <= x < f.rect(r)[2]
                    and f.rect(r)[1] <= y < f.rect(r)[3]]
            assert hits and all(r['attributes'].get('bundleName') == 'dev.nativeapi.smoke'
                                for r in hits), hits
            for root in hits:
                row = rows[int(root['attributes']['hostWindowId'])]
                assert row[2] == f.pid and int(row[7]) > 0
        f.identity()
        (f.out / f'{label}-owners.json').write_text(json.dumps(dict(
            pid=f.pid, uid=f.uid, points=points, windows=rows)))
        before = len(re.findall('NATIVEAPI_DROP_SDK_END', f.logs()))
        result = f.guest('uinput', '-T', '-g', *points[0], *points[1], 900, 2200)
        assert not re.search('failed|invalid|error', result, re.I), result
        deadline = time.monotonic() + 12
        while len(re.findall('NATIVEAPI_DROP_SDK_END', f.logs())) <= before:
            assert time.monotonic() < deadline, 'SDK source did not complete a real drag'
            time.sleep(.15)
        gestures += 1
        return points[1]

    command('setup')
    base = command('listen')
    destination = drag('copy')
    result = command('state')
    assert result['dropped'] == base['dropped'] + 1 and result['entered'] > base['entered']
    assert result['moved'] > base['moved'], result
    assert abs(result['x'] - (destination[0] / result['scale'] - result['contentX'])) < 8
    assert abs(result['y'] - (destination[1] / result['scale'] - result['contentY'])) < 8
    f.screenshot('copied')
    command('files')
    base = command('state')
    drag('files')
    result = command('state')
    assert result['dropped'] == base['dropped'] + 1
    command('files-only')
    base = command('state')
    drag('files-only')
    result = command('state')
    assert result['dropped'] == base['dropped'] + 1
    command('text')
    base = command('state')
    drag('leave-without-drop', leave=True)
    result = command('state')
    assert result['entered'] > base['entered'] and result['exited'] == base['exited'] + 1
    assert result['dropped'] == base['dropped']
    for operation in ['none', 'move', 'link']:
        base = command(operation)
        drag(operation)
        result = command('state')
        assert all(result[k] == base[k] for k in ['entered', 'moved', 'exited', 'dropped']), result
    command('copy')
    base = command('unlisten')
    drag('unregistered')
    result = command('state')
    assert all(result[k] == base[k] for k in ['entered', 'moved', 'exited', 'dropped']), result
    command('listen')
    base = command('self-remove')
    drag('self-remove')
    result = command('state')
    assert result['entered'] == base['entered'] + 1 and result['dropped'] == base['dropped']
    base = command('c-listen')
    drag('c-copy')
    result = command('state')
    assert result['dropped'] == base['dropped'] + 1 and result['cEvents'] > base['cEvents']
    command('files')
    base = command('state')
    drag('c-files')
    result = command('state')
    assert result['dropped'] == base['dropped'] + 1
    command('files-only')
    base = command('state')
    drag('c-files-only')
    result = command('state')
    assert result['dropped'] == base['dropped'] + 1
    command('text')
    base = command('c-remove')
    drag('c-unregistered')
    result = command('state')
    assert result['cEvents'] == base['cEvents'] and result['dropped'] == base['dropped']
    f.screenshot('complete')
    report = dict(pid=f.pid, uid=f.uid, kind=f.kind, gestures=gestures,
                  commands=commands, native=result, evidence=str(f.out))
    (f.out / 'result.json').write_text(json.dumps(report, indent=2))
    print('NATIVEAPI_DROP_PASS ' + json.dumps(report), flush=True)


if __name__ == '__main__':
    main()
