#!/usr/bin/env python3
"""Owned-guest drags from NativeAPI into an independent ArkTS SDK destination."""
import argparse
import json
import re
import time
import subprocess
from pathlib import Path
from ohos_ui_fixture import Fixture


def main():
    p = argparse.ArgumentParser()
    p.add_argument('deveco', type=Path)
    p.add_argument('--device', default='127.0.0.1:5555')
    a = p.parse_args()
    f = Fixture(a.deveco, a.device, 'drag-source', 'SOURCE')
    f.wait('NATIVEAPI_SOURCE_READY')
    commands = 0
    gestures = 0

    def command(name):
        nonlocal commands
        commands += 1
        nonce = f'source-{time.time_ns()}'
        f.guest('aa', 'start', '-a', 'EntryAbility', '-b', 'dev.nativeapi.smoke',
                '--ps', 'nativeapiSuite', 'drag-source', '--ps', 'nativeapiSourceCommand', name,
                '--ps', 'nativeapiSourceRequest', nonce)
        marker = f'NATIVEAPI_SOURCE_REPLY {{"request":"{nonce}"'
        records = f.wait(marker)
        result = json.loads(next(line.split('NATIVEAPI_SOURCE_REPLY ', 1)[1]
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
                    if key in ['nativeapi-source-pad', 'nativeapi-source-target']:
                        assert key not in nodes
                        nodes[key] = node
        assert len(nodes) == 2, nodes
        points = []
        for key in ['nativeapi-source-pad', 'nativeapi-source-target']:
            l, t, r, b = f.rect(nodes[key])
            assert r > l and b > t
            points.append(((l + r) // 2, b + 8 if leave and key == 'nativeapi-source-target' else (t + b) // 2))
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
        if 'snapshot' in label:
            before = f.screenshot(label + '-before')
            proc = subprocess.Popen(list(map(str, [f.hdc, '-t', f.device, 'shell', 'uinput', '-T', '-g',
                                      *points[0], *points[1], 900, 2200])),
                                    stdin=subprocess.DEVNULL, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
            held = None
            try:
                time.sleep(.35)
                held = f.screenshot(label + '-held')
            finally:
                result, _ = proc.communicate(timeout=30)  # Complete the atomic gesture, including release.
            assert proc.returncode == 0
            owner = next(r for r in tree['children'] if r['attributes'].get('bundleName') == 'dev.nativeapi.smoke')
            region = f.rect(owner)
            def magenta(image):
                return sum(r > 200 and g < 80 and b > 200 for r, g, b in image.crop(region).getdata())
            assert held is not None and magenta(held) > magenta(before) + 12, (label, magenta(before), magenta(held))
        elif label.endswith('-mouse'):
            result = f.guest('uinput', '-M', '-g', *points[0], *points[1], 800)
        else:
            result = f.guest('uinput', '-T', '-g', *points[0], *points[1], 900, 2200)
        assert not re.search('failed|invalid|error', result, re.I), result
        gestures += 1
        return points[1]

    command('setup')
    command('sdk-reference')
    end_point = drag('sdk-reference')
    assert end_point[0] > 0 and end_point[1] > 0
    f.wait('NATIVEAPI_SOURCE_REFERENCE_END 1 x=0.000000 y=0.000000')
    command('cpp-text')
    idle = command('idle')
    assert idle['ended'] == 0 and not idle['dragging']
    scenes = ['cpp-text', 'cpp-mixed', 'cpp-files', 'cpp-snapshot',
              'cpp-refuse', 'cpp-leave', 'cpp-target-move', 'cpp-destroy-end', 'cpp-destroy-live',
              'c-text', 'c-mixed', 'c-files', 'c-snapshot', 'c-target-move', 'c-destroy-end', 'c-destroy-live',
              'cpp-empty', 'cpp-none', 'cpp-move', 'cpp-link', 'cpp-invalid-file', 'cpp-nul',
              'cpp-worker-destroy', 'c-worker-destroy']
    if f.kind == '2in1':
        scenes += ['cpp-mouse', 'c-mouse']
    for scene in scenes:
        base = command(scene)
        drag(scene, leave=scene.endswith('leave'))
        accepted = scene not in ['cpp-empty', 'cpp-none', 'cpp-move', 'cpp-link', 'cpp-invalid-file', 'cpp-nul']
        delivers = accepted and 'destroy-live' not in scene and 'worker-destroy' not in scene
        f.wait('NATIVEAPI_SOURCE_START ' + str(base['starts'] + 1) + ' ')
        if delivers:
            f.wait('NATIVEAPI_SOURCE_ENDED ' + str(base['ended'] + 1))
        if 'worker-destroy' in scene:
            f.wait('NATIVEAPI_SOURCE_WORKER_RELEASED ' + str(base['workers'] + 1))
        result = command('state')
        assert result['starts'] == base['starts'] + 1 and result['lastReturn'] == accepted, (scene, base, result)
        assert not result['dragging'], (scene, result)
        assert result['ended'] == base['ended'] + int(delivers), (scene, result)
        if accepted and 'leave' not in scene:
            f.wait('NATIVEAPI_SOURCE_SDK_DROP ' + scene + ' ')
        print('PASS actual native source scene', scene, flush=True)
    f.screenshot('complete')
    report = dict(pid=f.pid, uid=f.uid, kind=f.kind, gestures=gestures, commands=commands,
                  native=result, evidence=str(f.out))
    (f.out / 'result.json').write_text(json.dumps(report, indent=2))
    print('NATIVEAPI_SOURCE_PASS ' + json.dumps(report), flush=True)


if __name__ == '__main__':
    main()
