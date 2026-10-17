#!/usr/bin/env python3
"""Cross-app pasteboard bytes via an independent ArkTS SDK peer, no host input."""
import argparse
import json
import re
import time
import zipfile
from pathlib import Path
from urllib.parse import unquote, urlparse
from ohos_ui_fixture import Fixture

PIXELS = [255, 0, 0, 255, 0, 255, 0, 255, 0, 0, 255, 255,
          255, 255, 255, 255, 0, 0, 0, 255, 255, 255, 0, 255]
PEER = 'dev.nativeapi.clipboard.peer'
MAIN = 'dev.nativeapi.smoke'


def grant(f, bundle):
    metadata = f.guest('bm', 'dump', '-n', bundle)
    data = json.loads(metadata[metadata.index('{'):])
    token = data['applicationInfo']['accessTokenId']
    assert isinstance(token, int)
    f.guest('atm', 'perm', '-g', '-i', str(token), '-p', 'ohos.permission.READ_PASTEBOARD')
    return data['applicationInfo']['uid']


class Interop:
    def __init__(self, root, device):
        self.f = Fixture(root, device, 'clipboard', 'CLIPBOARD')
        self.f.wait('NATIVEAPI_CLIPBOARD_READY')
        self.sequence = 0
        peer_hap = Path(__file__).resolve().parent.parent / 'ohos_clipboard_peer/entry/build/default/outputs/default/entry-default-unsigned.hap'
        with zipfile.ZipFile(peer_hap) as archive:
            manifest = json.loads(archive.read('module.json'))
            assert manifest['app']['bundleName'] == PEER
            assert manifest['module']['abilities'][0]['name'] == 'EntryAbility'
            assert self.f.kind in manifest['module']['deviceTypes']
            assert not any(name.endswith('.so') for name in archive.namelist()), 'Peer must use only the public ArkTS SDK'
        result = self.f.run(self.f.hdc, '-t', device, 'install', peer_hap)
        assert 'install bundle successfully' in result
        grant(self.f, MAIN)
        self.peer_uid = grant(self.f, PEER)
        self.f.guest('aa', 'force-stop', PEER)
        self.f.guest('aa', 'start', '-a', 'EntryAbility', '-b', PEER)
        deadline = time.monotonic() + 15
        self.peer_pid = ''
        while not re.fullmatch(r'\d+', self.peer_pid):
            assert time.monotonic() < deadline
            time.sleep(.2)
            self.peer_pid = self.f.guest('pidof', PEER).strip()
        assert self.peer_uid != self.f.uid and self.peer_pid != self.f.pid
        self.peer_identity()
        self.f.identity()

    def peer_identity(self):
        f = self.f
        assert f.guest('pidof', PEER).strip() == self.peer_pid
        assert f.guest('cat', f'/proc/{self.peer_pid}/cmdline').split('\0')[0] == PEER
        found = re.search(r'^Uid:\s+(\d+)\s+(\d+)\s+(\d+)\s+(\d+)',
                          f.guest('cat', f'/proc/{self.peer_pid}/status'), re.M)
        assert found and all(int(n) == self.peer_uid for n in found.groups())

    def command(self, command, peer=False):
        f = self.f
        self.peer_identity() if peer else f.identity()
        bundle = PEER if peer else MAIN
        # Bring the own app forward before issuing a read, so foreground policy
        # is genuinely satisfied rather than inferred from READ_PASTEBOARD alone.
        assert 'start ability successfully' in f.guest('aa', 'start', '-a', 'EntryAbility', '-b', bundle)
        time.sleep(.5)
        request = str(time.time_ns())
        parameters = ['--ps', 'command', command, '--ps', 'request', request] if peer else [
            '--ps', 'nativeapiSuite', 'clipboard', '--ps', 'nativeapiClipboardCommand', command,
            '--ps', 'nativeapiClipboardRequest', request]
        assert 'start ability successfully' in f.guest('aa', 'start', '-a', 'EntryAbility', '-b', bundle, *parameters)
        marker = ('NATIVEAPI_CLIPBOARD_PEER_REPLY ' if peer else 'NATIVEAPI_CLIPBOARD_REPLY ') + request + ' ' + command + ' '
        deadline = time.monotonic() + 30
        while time.monotonic() < deadline:
            if peer:
                self.peer_identity()
                records = f.guest('hilog', '-x', '-P', self.peer_pid)
                assert 'NATIVEAPI_CLIPBOARD_PEER_ERROR' not in records, records[-4500:]
            else:
                records = f.logs()
            rows = [line.split(marker, 1)[1] for line in records.splitlines() if marker in line]
            if rows:
                result = json.loads(rows[-1])
                assert result.get('failed', 0) == 0 and result.get('ok', True), result
                self.sequence += 1
                label = f'{self.sequence:02d}-' + ('peer-' if peer else '') + command
                (f.out / (label + '.json')).write_text(json.dumps(result, ensure_ascii=False, indent=2))
                (f.out / (label + '.log')).write_text(records)
                print('PASS command', label, result, flush=True)
                return result
            time.sleep(.1)
        raise AssertionError(('Missing fresh command reply', bundle, command))

    @staticmethod
    def payload(data, source, files=True):
        assert data['text'] == source + '-text-中文🙂', data
        assert data['html'] == '<b>' + source + '-html-中文🙂</b>', data
        assert data['pixels'] == PIXELS, data
        if files:
            assert data['contents'] == [f'{source}-payload-{i}-中文🙂' for i in range(2)], data
            assert len(data.get('paths', data.get('uris'))) == 2, data


def main():
    p = argparse.ArgumentParser()
    p.add_argument('deveco', type=Path)
    p.add_argument('--device', default='127.0.0.1:5555')
    a = p.parse_args()
    c = Interop(a.deveco, a.device)
    f = c.f
    try:
        seeded = c.command('validate-paths')['seededPaths']
        before = c.command('listen')
        reference = c.command('publish', peer=True)
        c.payload(reference, 'peer')
        assert [unquote(urlparse(uri).path) for uri in reference['uris']] == seeded, (reference, seeded)
        for command in ['read', 'read-c', 'read-worker']:
            data = c.command(command)
            c.payload(data, 'peer')
            assert data['cpp'] > before['cpp'] and data['c'] > before['c']
            assert all(not path.startswith('/data/storage/') for path in data['paths']), data
        files = c.command('files')
        assert files['contents'] == [f'peer-payload-{i}-中文🙂' for i in range(2)]
        assert files['text'] is None and files['html'] is None and files['pixels'] == []
        for command in ['publish', 'publish-c']:
            c.command(command)
            c.payload(c.command('read', peer=True), 'native')
        stopped = c.command('unlisten')
        assert not stopped['monitoring']
        c.command('publish', peer=True)
        after = c.command('read')
        c.payload(after, 'peer')
        assert [after[k] for k in ['cpp', 'c']] == [stopped[k] for k in ['cpp', 'c']]
        c.command('clear', peer=True)
        empty = c.command('read-c')
        assert empty['text'] is None and empty['html'] is None and not empty['paths'] and not empty['pixels']
        final = c.command('state')
        f.screenshot('complete')
        f.windows('complete')
        result = dict(pid=f.pid, peerPid=c.peer_pid, uid=f.uid, peerUid=c.peer_uid,
                      kind=f.kind, passed=final['passed'], failed=final['failed'], commands=c.sequence)
        (f.out / 'result.json').write_text(json.dumps(result, indent=2))
        print('NATIVEAPI_CLIPBOARD_INTEROP_PASS', result, 'evidence', f.out, flush=True)
    finally:
        # Both identities are our disposable fixtures. Stop subscriptions even
        # when a stronger payload assertion fails; preserve logs and app data.
        c.command('unlisten')


if __name__ == '__main__':
    main()
