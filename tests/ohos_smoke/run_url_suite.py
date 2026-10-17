#!/usr/bin/env python3
"""Launch four independent native URL cases, then inspect the real browser."""
import argparse
import json
import os
import re
import shlex
import subprocess
import sys
import time
from pathlib import Path


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('deveco', type=Path)
    parser.add_argument('--device', default='127.0.0.1:5555')
    args = parser.parse_args()
    assert re.fullmatch(r'127\.0\.0\.1:\d+', args.device)
    suffix = '.exe' if os.name == 'nt' else ''
    hdc = args.deveco / f'sdk/default/openharmony/toolchains/hdc{suffix}'
    emulator = args.deveco / f'tools/emulator/Emulator{suffix}'
    root = Path(__file__).resolve().parent
    out = root / 'test-results/url-ui'
    out.mkdir(parents=True, exist_ok=True)

    def run(*command):
        return subprocess.check_output([str(x) for x in command], text=True, encoding='utf-8',
                                       stdin=subprocess.DEVNULL, stderr=subprocess.STDOUT, timeout=120)

    def shell(command):
        return run(hdc, '-t', args.device, 'shell', command)

    run(hdc, 'tconn', args.device)
    assert shell('param get const.product.model').strip() == 'emulator'
    kind = shell('param get const.product.devicetype').strip()
    instances = [i for i in json.loads(run(emulator, '-list', '-details')) if i['isRunning'] == 'true']
    assert len(instances) == 1 and instances[0]['name'].startswith('NativeAPI_')
    assert kind in ('phone', '2in1') and instances[0]['deviceType'] == kind
    native_count = ui_count = 0
    manifest = []
    for binding, scheme in [('cpp', 'https'), ('c', 'http'), ('cpp', 'http'), ('c', 'https')]:
        marker = f'NativeAPI-URL-{kind}-{binding}-{scheme}-{time.time_ns()}'
        url = f'{scheme}://example.com/#{marker}'
        shell('aa force-stop dev.nativeapi.smoke')
        status = shell('aa start -a EntryAbility -b dev.nativeapi.smoke --ps nativeapiSuite url '
                       f'--ps nativeapiUrl {shlex.quote(url)} --ps nativeapiUrlBinding {binding}')
        assert 'start ability successfully' in status, status
        for _ in range(30):
            app_pid = shell('pidof dev.nativeapi.smoke').strip()
            if re.fullmatch(r'\d+', app_pid):
                break
            time.sleep(.1)
        else:
            raise AssertionError('Native URL host did not launch')
        try:
            result = run(sys.executable, root / 'verify_url_ui.py', args.deveco,
                         '--device', args.device, '--pid', app_pid, '--url', url)
        except subprocess.CalledProcessError as error:
            (out / f'{app_pid}-ui.log').write_text(error.output, encoding='utf-8')
            print(error.output, flush=True)
            raise
        (out / f'{app_pid}-ui.log').write_text(result, encoding='utf-8')
        report = (out / app_pid / 'runtime.log').read_text(encoding='utf-8')
        match = re.findall(r'NATIVEAPI_URL_PASS \((\d+) passed, 0 failed\)', report)
        assert match
        native_count += int(match[-1])
        ui_match = re.search(r'NATIVEAPI_URL_UI_PASS \((\d+) passed, 0 failed\)', result)
        assert ui_match
        ui_count += int(ui_match[1])
        manifest.append(dict(pid=int(app_pid), binding=binding, url=url))
        (out / f'{kind}-cases.json').write_text(json.dumps(manifest, indent=2), encoding='utf-8')
        print(f'PASS {kind} {binding} {scheme}: native result and exact browser address verified (PID {app_pid})', flush=True)
    print(f'NATIVEAPI_URL_SUITE_PASS ({native_count} native checks, {ui_count} UI checks, 4 launches)', flush=True)


if __name__ == '__main__':
    main()
