#!/usr/bin/env python3
"""Pure value/ABI contracts plus SDK-driven live window positioning/lifetime."""
import argparse
import json
from pathlib import Path
from ohos_ui_fixture import Fixture


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('deveco', type=Path)
    parser.add_argument('--device', default='127.0.0.1:5555')
    args = parser.parse_args()
    f = Fixture(args.deveco, args.device, 'foundation', 'FOUNDATION')
    assert f.instance['name'] in ['NativeAPI_Phone', 'NativeAPI_Desktop']
    metadata = f.guest('bm', 'dump', '-n', f.bundle)
    app = json.loads(metadata[metadata.index('{'):])['applicationInfo']
    assert not app['isSystemApp'] and app['appPrivilegeLevel'] == 'normal'
    records = f.wait('NATIVEAPI_FOUNDATION_DONE')
    phases = []
    for line in records.splitlines():
        if 'NATIVEAPI_FOUNDATION_PHASE ' not in line:
            continue
        name, payload = line.split('NATIVEAPI_FOUNDATION_PHASE ', 1)[1].split(' ', 1)
        data = json.loads(payload)
        assert data['native']['failed'] == 0
        for key in ['cpp', 'copy', 'c']:
            assert all(abs(x-y) < .002 for x, y in zip(data['sdk'], data['native'][key]))
        (f.out / f'{name}.json').write_text(json.dumps(data, indent=2))
        phases.append(data)
    assert len(phases) == 2 and phases[0]['sdk'] != phases[1]['sdk']
    done = next(line.split('NATIVEAPI_FOUNDATION_DONE ', 1)[1] for line in records.splitlines() if 'NATIVEAPI_FOUNDATION_DONE ' in line)
    report = json.loads(done)
    assert report['failed'] == report['ui']['failed'] == report['worker']['failed'] == report['window']['failed'] == 0
    assert report['ui']['passed'] == report['worker']['passed'] and report['ui']['passed'] >= 150
    assert report['window']['passed'] >= 9 and not report['window']['ready']
    report.update(guest=f.instance['name'], pid=f.pid, uid=f.uid, evidence=str(f.out))
    (f.out/'summary.json').write_text(json.dumps(report, indent=2))
    print('PASS', json.dumps(report), flush=True)


if __name__ == '__main__':
    main()
