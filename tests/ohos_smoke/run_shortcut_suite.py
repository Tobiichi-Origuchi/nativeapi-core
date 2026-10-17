#!/usr/bin/env python3
"""Native hotkey registration and independent-app conflicts; no key injection."""
import argparse
import json
import re
import time
import zipfile
from pathlib import Path
from ohos_ui_fixture import Fixture

PEER = 'dev.nativeapi.clipboard.peer'


def main():
    p = argparse.ArgumentParser()
    p.add_argument('deveco', type=Path)
    p.add_argument('--device', default='127.0.0.1:5555')
    a = p.parse_args()
    f = Fixture(a.deveco, a.device, 'shortcuts', 'SHORTCUT')
    f.wait('NATIVEAPI_SHORTCUT_READY')
    hap = Path(__file__).resolve().parent.parent / 'ohos_clipboard_peer/entry/build/default/outputs/default/entry-default-unsigned.hap'
    with zipfile.ZipFile(hap) as archive:
        manifest = json.loads(archive.read('module.json'))
        assert manifest['app']['bundleName'] == PEER
        assert not any(p.endswith('.so') for p in archive.namelist())
    assert 'install bundle successfully' in f.run(f.hdc, '-t', a.device, 'install', hap)
    metadata = f.guest('bm', 'dump', '-n', PEER)
    (f.out / 'peer-bundle-metadata.json').write_text(metadata)
    peer_uid = json.loads(metadata[metadata.index('{'):])['applicationInfo']['uid']
    assert peer_uid != f.uid
    f.guest('aa', 'force-stop', PEER)
    f.guest('aa', 'start', '-a', 'EntryAbility', '-b', PEER)
    time.sleep(1)
    peer_pid = f.guest('pidof', PEER).strip()
    assert re.fullmatch(r'\d+', peer_pid) and peer_pid != f.pid
    sequence = 0

    def peer_identity():
        assert f.guest('pidof', PEER).strip() == peer_pid
        assert f.guest('cat', f'/proc/{peer_pid}/cmdline').split('\0')[0] == PEER
        uid = re.search(r'^Uid:\s+(\d+)\s+(\d+)\s+(\d+)\s+(\d+)',
                        f.guest('cat', f'/proc/{peer_pid}/status'), re.M)
        assert uid and all(int(n) == peer_uid for n in uid.groups())

    def command(action, peer=False):
        nonlocal sequence
        peer_identity() if peer else f.identity()
        request = str(time.time_ns())
        parameters = ['--ps', 'command', action, '--ps', 'request', request] if peer else [
            '--ps', 'nativeapiSuite', 'shortcuts', '--ps', 'nativeapiShortcutCommand', action,
            '--ps', 'nativeapiShortcutRequest', request]
        assert 'start ability successfully' in f.guest('aa', 'start', '-a', 'EntryAbility', '-b',
                                                       PEER if peer else 'dev.nativeapi.smoke', *parameters)
        marker = ('NATIVEAPI_SHORTCUT_PEER_REPLY ' if peer else 'NATIVEAPI_SHORTCUT_REPLY ') + request + ' ' + action + ' '
        deadline = time.monotonic() + 25
        while time.monotonic() < deadline:
            peer_identity() if peer else f.identity()
            records = f.guest('hilog', '-x', '-P', peer_pid) if peer else f.logs()
            rows = [line.split(marker, 1)[1] for line in records.splitlines() if marker in line]
            if rows:
                result = json.loads(rows[-1])
                assert result.get('failed', 0) == 0, result
                sequence += 1
                label = f'{sequence:02d}-' + ('peer-' if peer else '') + action
                (f.out / (label + '.json')).write_text(json.dumps(result, indent=2))
                (f.out / (label + '.log')).write_text(records)
                print('PASS command', label, result, flush=True)
                return result
            time.sleep(.1)
        raise AssertionError(('Missing request', action))

    try:
        command('setup')
        command('register')
        command('worker')
        occupied = command('hotkey-acquire', peer=True)
        assert occupied['code'] == 4200003 and occupied['held'] == 0, occupied
        command('release')
        acquired = command('hotkey-acquire', peer=True)
        assert acquired['code'] == 0 and acquired['held'] == 4, acquired
        command('conflict')
        released = command('hotkey-release', peer=True)
        assert released['code'] == 0 and released['held'] == 0
        command('register')
        command('clear')
        drained = command('state')
        assert drained['registered'] == drained['unregistered'] == 3, drained
        command('finish')
        final = command('state')
        assert final['count'] == 0 and final['callbacks'] == final['activated'] == 0
        assert final['registered'] == final['unregistered'] == 3, final
        assert final['rejected'] >= 10
        f.screenshot('registration-complete')
        f.windows('registration-complete')
        result = dict(pid=f.pid, peerPid=peer_pid, uid=f.uid, peerUid=peer_uid, kind=f.kind,
                      passed=final['passed'], failed=final['failed'], commands=sequence)
        (f.out / 'result.json').write_text(json.dumps(result, indent=2))
        print('NATIVEAPI_SHORTCUT_REGISTRATION_PASS', result, 'evidence', f.out, flush=True)
    finally:
        command('hotkey-release', peer=True)
        command('finish')


if __name__ == '__main__':
    main()
