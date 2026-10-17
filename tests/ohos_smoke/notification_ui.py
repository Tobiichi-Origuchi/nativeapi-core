#!/usr/bin/env python3
"""Guarded notification interactions in the named disposable guest only."""
import argparse
import json
import re
import subprocess
import time
from pathlib import Path

p = argparse.ArgumentParser()
p.add_argument('deveco', type=Path)
p.add_argument('--pid', type=int, required=True)
p.add_argument('--action', choices=['inspect', 'authorize', 'allow', 'deny', 'dismiss-shortcut', 'panel', 'expand', 'button', 'body'], default='inspect')
p.add_argument('--device', default='127.0.0.1:5555')
p.add_argument('--cold', action='store_true')
p.add_argument('--tag', choices=['probe','body','cold_open','cold_button'])
p.add_argument('--bundle', default='dev.nativeapi.smoke')
a = p.parse_args()
assert re.fullmatch(r'127\.0\.0\.1:\d+', a.device)
assert (a.bundle=='dev.nativeapi.smoke' or re.fullmatch(r'dev\.nativeapi\.notificationdenied[0-9]*',a.bundle) or
        (a.action=='dismiss-shortcut' and a.bundle=='dev.nativeapi.keyboard.monitor'))
hdc = a.deveco / 'sdk/default/openharmony/toolchains/hdc'
emulator = a.deveco / 'tools/emulator/Emulator'


def run(*cmd):
    return subprocess.check_output([str(x) for x in cmd], stdin=subprocess.DEVNULL, text=True,
                                   encoding='utf-8', stderr=subprocess.STDOUT, timeout=40)


def guest(*cmd):
    return run(hdc, '-t', a.device, 'shell', *cmd)


def app_identity():
    current=guest('pidof', a.bundle).strip()
    if a.cold:
        assert not current, 'Cold click requires no running fixture process'
    else:
        assert current and int(current)==a.pid


app_identity()
assert guest('param', 'get', 'const.product.model').strip() == 'emulator'
instances = [i for i in json.loads(run(emulator, '-list', '-details')) if i['isRunning'] == 'true']
assert len(instances) == 1 and instances[0]['name'].startswith('NativeAPI_')
instance = instances[0]
kind = guest('param', 'get', 'const.product.devicetype').strip()
assert kind == instance['deviceType'] and kind in ('phone', '2in1')
metadata=guest('bm','dump','-n',a.bundle)
app_info=json.loads(metadata[metadata.index('{'):])['applicationInfo']
fixture_uid=app_info['uid']
fixture_label=('NativeAPI Keyboard Monitor' if a.bundle=='dev.nativeapi.keyboard.monitor' else
               'NativeAPI Notification Denial' if a.bundle.startswith('dev.nativeapi.notificationdenied') else 'NativeAPI Smoke')
out = Path(__file__).resolve().parent / 'test-results/notification-ui' / str(a.pid)
out.mkdir(parents=True, exist_ok=True)
label = f'{time.time_ns()}-{a.action}'
status = guest('uitest', 'dumpLayout', '-p', '/data/local/tmp/nativeapi-notification.json')
assert 'DumpLayout saved' in status
local = out / f'{label}-ownership.json'
run(hdc, '-t', a.device, 'file', 'recv', '/data/local/tmp/nativeapi-notification.json', local)
tree = json.loads(local.read_text(encoding='utf-8'))
windows = guest('hidumper', '-s', 'WindowManagerService', '-a', '-a')
(out / f'{label}-windows.txt').write_text(windows, encoding='utf-8')


def walk(n):
    yield n
    for child in n.get('children', []):
        yield from walk(child)


def rect(n):
    values = list(map(int, re.findall(r'-?\d+', n['attributes']['bounds'])))
    assert len(values) == 4
    return values


def press(root, node, slide=False):
    bundle = root['attributes']['bundleName']
    assert bundle in ('com.ohos.notificationdialog', 'com.ohos.sceneboard')
    window_id = root['attributes']['hostWindowId']
    rows = [line.split() for line in windows.splitlines()
            if re.match(r'^\S+\s+\d+\s+\d+\s+' + re.escape(window_id) + r'\s+', line)]
    assert len(rows) == 1 and int(rows[0][7]) > 0
    if bundle == 'com.ohos.notificationdialog':
        pid = rows[0][2]
        process = guest('cat', f'/proc/{pid}/cmdline').split('\0')[0]
        assert re.fullmatch(re.escape(bundle) + r':EnableNotificationDialog:\d+', process)
        metadata = guest('bm', 'dump', '-n', bundle)
        uid = json.loads(metadata[metadata.index('{'):])['applicationInfo']['uid']
        status = guest('cat', f'/proc/{pid}/status')
        match = re.search(r'^Uid:\s+(\d+)\s+(\d+)\s+(\d+)\s+(\d+)', status, re.M)
        assert match and all(int(value) == uid for value in match.groups())
    else:
        assert rows[0][2] in guest('pidof', bundle).split()
    left, top, right, bottom = rect(node)
    assert right > left and bottom > top
    x, y = (left + right) // 2, (top + bottom) // 2
    hits = [r for r in tree['children'] if rect(r)[0] <= x < rect(r)[2] and rect(r)[1] <= y < rect(r)[3]]
    assert len(hits) == 1 and hits[0]['attributes']['hostWindowId'] == window_id
    app_identity()
    (out / f'{label}-press.json').write_text(json.dumps(dict(
        bundle=bundle, pid=int(rows[0][2]), window=int(window_id), x=x, y=y,
        action=a.action, node=node['attributes'])), encoding='utf-8')
    if slide:
        height = int(instance['hw.lcd.single.height'])
        end_y = height * 2 // 3
        assert 0 <= x < int(instance['hw.lcd.single.width']) and y < end_y < height
        print(run(emulator, '-instance', instance['name'], '-slide', f'{x} {y} {x} {end_y}').strip())
    else:
        print(run(emulator, '-instance', instance['name'], '-click', f'{x} {y}').strip())


roots = tree.get('children', [])
if a.action == 'inspect':
    for r in roots:
        attrs = r['attributes']
        if attrs.get('bundleName') not in ('com.ohos.sceneboard', 'com.ohos.notificationdialog'):
            continue
        print('ROOT', {k: attrs.get(k) for k in ['bundleName', 'hostWindowId', 'bounds']})
        for n in walk(r):
            at = n['attributes']
            if at.get('text') or re.search(r'notif|bell|clock|date|time', at.get('id', ''), re.I):
                print({k: at.get(k) for k in ['type', 'text', 'id', 'bounds', 'clickable']})
elif a.action == 'dismiss-shortcut':
    assert (a.bundle in ['dev.nativeapi.smoke', 'dev.nativeapi.keyboard.monitor'] or a.bundle.startswith('dev.nativeapi.notificationdenied')) and not a.cold
    matches = []
    for r in roots:
        if r['attributes'].get('bundleName') != 'com.ohos.sceneboard':
            continue
        texts = [n['attributes'].get('text', '') for n in walk(r)]
        if '是否将应用自动添加至桌面？' not in texts or not any(fixture_label in text for text in texts):
            continue
        matches.extend((r, n) for n in walk(r) if n['attributes'].get('type') == 'Text' and n['attributes'].get('text') == '不添加')
    if not matches:
        print('No own installation shortcut prompt is visible')
        raise SystemExit(0)
    assert len(matches) == 1
    press(*matches[0])
elif a.action in ('authorize', 'allow', 'deny'):
    roots = [r for r in roots if r['attributes'].get('bundleName') == 'com.ohos.notificationdialog']
    if not roots and a.action == 'authorize':
        print('No notification authorization dialog is visible')
        raise SystemExit(0)
    assert len(roots) == 1
    root = roots[0]
    assert any(fixture_label in n['attributes'].get('text', '') for n in walk(root))
    text = '不允许' if a.action == 'deny' else '允许'
    matches = [n for n in walk(root) if n['attributes'].get('type') == 'Button' and n['attributes'].get('text') == text]
    assert len(matches) == 1
    assert re.search(r'^Focus window:\s*' + root['attributes']['hostWindowId'] + r'\s*$', windows, re.M)
    press(root, matches[0])
elif a.action == 'panel':
    if any(n['attributes'].get('id') in ('SCBAssemblyPanel_Column_NotificationPanel', '[Dropdown]NotificationListComponent')
           for r in roots if r['attributes'].get('bundleName') == 'com.ohos.sceneboard'
           for n in walk(r)):
        print('Notification center is already open')
        raise SystemExit(0)
    identifier = ('PluginRootComponent_Stack_status_bar_notification_panel' if kind == '2in1'
                  else 'StatusBarIconWrapper_status_bar_clock')
    matches = [(r, n) for r in roots if r['attributes'].get('bundleName') == 'com.ohos.sceneboard'
               for n in walk(r) if n['attributes'].get('id') == identifier]
    assert len(matches) == 1
    press(*matches[0], slide=kind == 'phone')
elif a.action in ('expand', 'button', 'body'):
    tag = a.tag or ('body' if a.action == 'body' else 'probe')
    button_label='NativeAPI cold action' if tag=='cold_button' else 'NativeAPI action'
    matches = []
    for root in roots:
        if root['attributes'].get('bundleName') != 'com.ohos.sceneboard':
            continue
        cards = [n for n in walk(root) if n['attributes'].get('type') == 'ListItem' and
                 re.search(r'NotificationListVm_Item_.*_' + str(fixture_uid) + '_' + re.escape(a.bundle) + r'_nativeapi:' + tag + r'_20033$', n['attributes'].get('id', ''))]
        for card in cards:
            if a.action == 'expand' and any(n['attributes'].get('text') == button_label for n in walk(card)):
                print('Owned notification button is already expanded')
                raise SystemExit(0)
            if a.action == 'button':
                for n in walk(card):
                    if n['attributes'].get('text') == button_label:
                        matches.append((root, n))
            else:
                expected_title = {'probe':'NativeAPI notification updated','body':'NativeAPI open notification','cold_open':'NativeAPI cold open','cold_button':'NativeAPI cold button'}[tag]
                assert any(n['attributes'].get('text') == expected_title for n in walk(card))
                matches.append((root, card))
    assert len(matches) == 1, f'Expected exactly one owned notification target: {a.action}, got {len(matches)}'
    press(*matches[0])
else:
    raise AssertionError('Interaction selector not yet established from the current native tree')
