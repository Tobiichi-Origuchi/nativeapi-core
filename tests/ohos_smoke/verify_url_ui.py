#!/usr/bin/env python3
"""Verify native/browser state using guarded address/edit/back controls only."""
import argparse
import json
import os
import re
import subprocess
import time
from urllib.parse import urlsplit
from pathlib import Path


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('deveco', type=Path)
    parser.add_argument('--device', default='127.0.0.1:5555')
    parser.add_argument('--pid', required=True, type=int)
    parser.add_argument('--url', required=True)
    args = parser.parse_args()
    assert re.fullmatch(r'127\.0\.0\.1:\d+', args.device)
    suffix = '.exe' if os.name == 'nt' else ''
    hdc = args.deveco / f'sdk/default/openharmony/toolchains/hdc{suffix}'
    emulator = args.deveco / f'tools/emulator/Emulator{suffix}'
    out = Path(__file__).resolve().parent / 'test-results/url-ui' / str(args.pid)
    out.mkdir(parents=True, exist_ok=True)

    def run(*command):
        return subprocess.check_output([str(x) for x in command], text=True, encoding='utf-8',
                                       stdin=subprocess.DEVNULL, stderr=subprocess.STDOUT, timeout=30)

    def shell(*command):
        return run(hdc, '-t', args.device, 'shell', *command)

    def alive():
        assert int(shell('pidof', 'dev.nativeapi.smoke').strip()) == args.pid

    run(hdc, 'tconn', args.device)
    assert shell('param', 'get', 'const.product.model').strip() == 'emulator'
    instances = [i for i in json.loads(run(emulator, '-list', '-details')) if i['isRunning'] == 'true']
    assert len(instances) == 1 and instances[0]['name'].startswith('NativeAPI_')
    instance = instances[0]
    kind = shell('param', 'get', 'const.product.devicetype').strip()
    assert kind in ('phone', '2in1') and instance['deviceType'] == kind
    for _ in range(100):
        alive()
        log = shell('hilog', '-x', '-T', 'nativeapi-url', '-P', args.pid)
        (out / 'runtime.log').write_text(log, encoding='utf-8')
        assert 'NATIVEAPI_URL_FAIL' not in log, log
        begin = log.rfind(f'NATIVEAPI_URL_BEGIN {args.url}')
        if begin >= 0 and re.search(r'NATIVEAPI_URL_PASS \(\d+ passed, 0 failed\)', log[begin:]):
            break
        time.sleep(.2)
    else:
        raise AssertionError('Native URL suite did not finish')
    assert f'NATIVEAPI_URL_BEGIN {args.url}' in log, 'Native test requested a different URL'
    print('PASS native SDK completion and full URL test result belong to the fresh process')

    normalized = args.url.strip()
    scheme, rest = normalized.split(':', 1)
    expected = scheme.lower() + ':' + rest
    displayed = expected.split('://', 1)[1]

    def visit(node):
        yield node
        for child in node.get('children', []):
            yield from visit(child)

    def owned_window(root, label, bundle='com.huawei.hmos.browser', focus=True):
        browser_pids = shell('pidof', bundle).split()
        window_id = root['attributes']['hostWindowId']
        windows = shell('hidumper', '-s', 'WindowManagerService', '-a', '-a')
        (out / f'{label}-windows.txt').write_text(windows, encoding='utf-8')
        own = [line.split() for line in windows.splitlines()
               if re.match(r'^\S+\s+\d+\s+\d+\s+' + re.escape(window_id) + r'\s+', line)]
        assert len(own) == 1 and int(own[0][7]) > 0
        if bundle == 'com.huawei.hmos.inputmethod':
            # The keyboard is an extension process, not the first-run app's
            # main PID. Bind its exact service name and UID to bundle metadata.
            assert own[0][4] == '2105'
            pid = own[0][2]
            process = shell('cat', f'/proc/{pid}/cmdline').split('\0')[0].strip()
            assert process == bundle + ':inputMethod'
            metadata = shell('bm', 'dump', '-n', bundle)
            installed = json.loads(metadata[metadata.index('{'):])
            uid = installed['applicationInfo']['uid']
            status = shell('cat', f'/proc/{pid}/status')
            match = re.search(r'^Uid:\s+(\d+)\s+(\d+)\s+(\d+)\s+(\d+)', status, re.M)
            assert match and all(int(value) == uid for value in match.groups())
            (out / f'{label}-keyboard-identity.json').write_text(
                json.dumps(dict(pid=int(pid), process=process, uid=uid)), encoding='utf-8')
        else:
            assert own[0][2] in browser_pids
        if focus:
            assert re.search(r'^Focus window:\s*' + re.escape(window_id) + r'\s*$', windows, re.M)

    def press_owned(root, attributes, tree, label, bundle='com.huawei.hmos.browser', focus=True):
        owned_window(root, label, bundle, focus)
        rect = re.fullmatch(r'\[(-?\d+),(-?\d+)\]\[(-?\d+),(-?\d+)\]', attributes['bounds'])
        assert rect
        left, top, right, bottom = map(int, rect.groups())
        assert right > left and bottom > top
        x, y = (left + right) // 2, (top + bottom) // 2
        hits = []
        for n in tree['children']:
            b = list(map(int, re.findall(r'-?\d+', n['attributes']['bounds'])))
            if b[0] <= x < b[2] and b[1] <= y < b[3]:
                hits.append(n['attributes'])
        assert len(hits) == 1 and hits[0].get('bundleName') == bundle and \
            hits[0]['hostWindowId'] == root['attributes']['hostWindowId']
        alive()
        run(emulator, '-instance', instance['name'], '-click', f'{x} {y}')

    def chrome(node, web=False):
        a = node.get('attributes', {})
        web = web or a.get('type') in ('Web', 'rootWebArea')
        if not web:
            yield node
            for child in node.get('children', []):
                yield from chrome(child, web)

    focused_address = False
    edited_address = False
    keyboard_hides = 0
    for attempt in range(25):
        alive()
        remote = '/data/local/tmp/nativeapi-url-ui.json'
        status = shell('uitest', 'dumpLayout', '-p', remote)
        if 'DumpLayout saved' not in status:
            time.sleep(.2)
            continue
        local = out / f'{attempt:02d}-ownership.json'
        run(hdc, '-t', args.device, 'file', 'recv', remote, local)
        tree = json.loads(local.read_text(encoding='utf-8'))
        roots = [n for n in tree.get('children', [])
                 if n.get('attributes', {}).get('bundleName') == 'com.huawei.hmos.browser']
        keyboards = [(r, n['attributes']) for r in tree.get('children', []) if
                     r.get('attributes', {}).get('bundleName') == 'com.huawei.hmos.inputmethod'
                     for n in visit(r) if n.get('attributes', {}).get('id') == 'KeyHideKbd' and
                     n['attributes'].get('visible') == 'true']
        if kind == 'phone' and focused_address and keyboards:
            assert len(keyboards) == len(roots) == 1 and keyboard_hides < 2
            owned_window(roots[0], f'before-keyboard-hide-{keyboard_hides}')
            # This is the keyboard's hide control, never a letter/key event.
            press_owned(keyboards[0][0], keyboards[0][1], tree, f'hide-keyboard-{keyboard_hides}',
                        'com.huawei.hmos.inputmethod', focus=False)
            keyboard_hides += 1
            continue
        matches = []
        for root in roots:
            for node in visit(root):
                a = node.get('attributes', {})
                if a.get('type') == 'TextInput' and a.get('text') in (expected, displayed):
                    matches.append((root, a))
        if len(matches) == 1:
            break
        if kind == 'phone' and focused_address and not edited_address:
            edits = [(r, n['attributes']) for r in roots for n in chrome(r)
                     if n.get('attributes', {}).get('id') == 'text_in_menu' and
                     n['attributes'].get('text') == '编辑']
            if len(edits) == 1:
                # Browser search initially leaves its input empty. Its Edit
                # action inserts the current page URL without typing anything.
                press_owned(edits[0][0], edits[0][1], tree, 'edit-current-address')
                edited_address = True
                continue
        if kind == 'phone' and not focused_address:
            bars = [(r, n['attributes']) for r in roots for n in chrome(r)
                    if n.get('attributes', {}).get('id') == 'search_box_in_web' and
                    n['attributes'].get('text') == urlsplit(expected).netloc]
            if len(bars) == 1:
                root, a = bars[0]
                # A single owned browser-chrome click exposes its prefilled URL.
                # No host pointer or guest/host keyboard input is used.
                press_owned(root, a, tree, 'before-address-focus')
                focused_address = True
        time.sleep(.2)
    else:
        raise AssertionError('Browser address field did not expose the exact requested URL')
    root, address = matches[0]
    owned_window(root, 'verified')
    print('PASS the real system browser owns the focused native window')
    (out / 'address.json').write_text(json.dumps(address, ensure_ascii=False, indent=2), encoding='utf-8')
    if kind == 'phone':
        back = [n['attributes'] for n in chrome(root) if
                n.get('attributes', {}).get('id') == 'back_btn_in_search']
        assert len(back) == 1, 'Native browser search back control missing'
        press_owned(root, back[0], tree, 'restore-page')
    status = run(emulator, '-instance', instance['name'], '-uiLayout', '-i')
    assert 'Scenario simulation success' in status
    layout = Path(instance['instancePath']) / 'uiLayout/analysis.md'
    (out / 'layout.md').write_text(layout.read_text(encoding='utf-8'), encoding='utf-8')
    print('PASS native browser address field preserves the exact requested URL and marker')
    print('NATIVEAPI_URL_UI_PASS (3 passed, 0 failed)')


if __name__ == '__main__':
    main()
