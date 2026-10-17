#!/usr/bin/env python3
"""Real display notifications; commands stay inside the verified disposable app."""
import argparse
import json
import time
from pathlib import Path
from ohos_ui_fixture import Fixture


class Controller:
    def __init__(self, root, device):
        self.f = Fixture(root, device, 'displays', 'DISPLAY')
        initial_log = self.f.wait('NATIVEAPI_DISPLAY_READY')
        (self.f.out / 'setup-native.log').write_text(initial_log)
        self.sequence = 0
        self.balance = 0
        self.initial = self.command('verify')

    def receive(self, action, request):
        marker = 'NATIVEAPI_DISPLAY_REPLY ' + request + ' ' + action + ' '
        deadline = time.monotonic() + 35
        while time.monotonic() < deadline:
            text = self.f.logs()
            rows = [line.split(marker, 1)[1] for line in text.splitlines() if marker in line]
            if rows:
                state = json.loads(rows[-1])
                assert state['failed'] == 0 and state['sdkCode'] == 0, state
                assert state['id'] == getattr(self, 'initial', state)['id']
                self.sequence += 1
                (self.f.out / f'{self.sequence:03d}-{action}.json').write_text(json.dumps(state, indent=2))
                return state
            time.sleep(.1)
        raise AssertionError(('Missing fresh app command response', action))

    def send(self, action):
        request = str(time.time_ns())
        self.f.identity()
        response = self.f.guest('aa', 'start', '-a', 'EntryAbility', '-b', 'dev.nativeapi.smoke',
                                '--ps', 'nativeapiSuite', 'displays',
                                '--ps', 'nativeapiDisplayCommand', action,
                                '--ps', 'nativeapiDisplayRequest', request)
        assert 'start ability successfully' in response, response
        self.f.identity()
        return request

    def command(self, action):
        request = self.send(action)
        return self.receive(action, request)

    @staticmethod
    def physical(state):
        return [state[k] for k in ['rotation', 'orientation', 'width', 'height', 'scale']]

    @staticmethod
    def counters(state):
        return [state[k] for k in ['cpp', 'typed', 'c', 'async', 'sdkChanged', 'sdkArea']]

    def settled(self):
        previous = self.command('verify')
        for _ in range(30):
            time.sleep(.2)
            current = self.command('verify')
            if self.counters(previous) == self.counters(current):
                return current
            previous = current
        raise AssertionError('Display callbacks did not settle')

    def turn(self, direction):
        self.f.identity()
        result = self.f.run(self.f.emulator, '-instance', self.f.instance['name'], '-rotation', direction)
        (self.f.out / f'{self.sequence:03d}-rotation-{direction}.txt').write_text(result)
        assert 'Scenario simulation success' in result, result
        self.balance += 1 if direction == 'left' else -1

    def transition(self, label, before, changed, listening, c_sync):
        deadline = time.monotonic() + 20
        while True:
            after = self.settled()
            sdk = after['sdkChanged'] + after['sdkArea'] - before['sdkChanged'] - before['sdkArea']
            delta = {key: after[key] - before[key] for key in ['cpp', 'typed', 'c', 'async']}
            expected = sdk if listening else 0
            if changed(before, after) and sdk > 0 and all(
                    delta[k] == (expected if k != 'c' or c_sync else 0) for k in delta):
                break
            assert time.monotonic() < deadline, (label, before, after, delta)
        expected_listeners = (4 if c_sync else 3) if listening else 0
        assert after['listeners'] == expected_listeners
        assert after['added'] == after['removed'] == 0
        (self.f.out / (label + '-native.log')).write_text(self.f.logs())
        self.f.windows(label)
        (self.f.out / f'{label}-service.txt').write_text(
            self.f.guest('hidumper', '-s', 'DisplayManagerService', '-a', '-a'))
        self.f.screenshot(label)
        print('PASS actual display transition', label, self.physical(after), after.get('area'), delta, flush=True)
        return after

    def rotate(self, label, direction, listening=True, c_sync=True):
        before = self.settled()
        self.turn(direction)
        return self.transition(label, before, lambda b, a: self.physical(b) != self.physical(a),
                               listening, c_sync)

    def fullscreen(self, label, enabled, listening=True, c_sync=True):
        before = self.settled()
        self.command('fullscreen' if enabled else 'windowed')
        return self.transition(label, before, lambda b, a: a['full'] == enabled and b['area'] != a['area'],
                               listening, c_sync)

    def restore(self):
        while self.balance:
            self.turn('right' if self.balance > 0 else 'left')
        after = self.settled()
        if after.get('full') != self.initial.get('full'):
            self.command('fullscreen' if self.initial['full'] else 'windowed')
            for _ in range(20):
                time.sleep(.2)
                after = self.settled()
                if after['full'] == self.initial['full'] and after['area'] == self.initial['area']:
                    break
        assert self.physical(after) == self.physical(self.initial), (self.initial, after)
        if 'area' in self.initial:
            assert after['area'] == self.initial['area'] and after['full'] == self.initial['full']


def main():
    p = argparse.ArgumentParser()
    p.add_argument('deveco', type=Path)
    p.add_argument('--device', default='127.0.0.1:5555')
    p.add_argument('--mode', choices=['rotation', 'fullscreen', 'quiet'], required=True)
    a = p.parse_args()
    c = Controller(a.deveco, a.device)
    f = c.f
    try:
        state = c.settled()
        assert state['listeners'] == 4
        if a.mode in ['rotation', 'fullscreen']:
            desktop = a.mode == 'fullscreen'
            assert f.kind == ('2in1' if desktop else 'phone')
            change = c.fullscreen if desktop else c.rotate
            left, right = (True, False) if desktop else ('left', 'right')
            change('left', left)
            change('right', right)
            c.command('remove-c')
            change('partial-unsubscribe', left, c_sync=False)
            c.command('unlisten-worker')
            change('unsubscribed', right, listening=False)
            c.command('listen-worker')
            change('resubscribed', left)
            before = c.settled()
            # Pause the fixture scheduler, leaving SceneBoard and the UI free.
            # The public NDK still produces every notification; none is fabricated.
            c.command('hold-queue')
            try:
                if desktop:
                    c.command('windowed')
                else:
                    c.turn('right')
                deadline = time.monotonic() + 20
                while True:
                    queued = c.settled()
                    changed = queued['area'] != before['area'] if desktop else c.physical(queued) != c.physical(before)
                    if (changed and queued['pending'] > 0 and
                            queued['sdkChanged'] + queued['sdkArea'] > before['sdkChanged'] + before['sdkArea']):
                        break
                    assert time.monotonic() < deadline, (before, queued)
                keys = ['cpp', 'typed', 'c', 'async']
                assert [queued[k] for k in keys] == [before[k] for k in keys]
                c.command('replace-queue')
            finally:
                c.command('release-queue')
            after = c.settled()
            assert [after[k] for k in keys] == [before[k] for k in keys]
            assert after['listeners'] == 4 and after['pending'] == 0
            (f.out / 'queued-notice-native.log').write_text(f.logs())
            print('PASS real queued notification invalidated across unsubscribe/resubscribe', queued['pending'], flush=True)
            change('new-observer', left)
            change('restored', right)
        else:
            # This is explicitly registration/quiet-state coverage, not proof of
            # positive display notifications on fixed-orientation computer images.
            for action, count in [('remove-c', 3), ('unlisten-worker', 0), ('listen-worker', 4),
                                  ('unlisten', 0), ('listen', 4)]:
                before = c.settled()
                c.command(action)
                after = c.settled()
                assert after['listeners'] == count
                assert c.counters(after) == c.counters(before), (action, before, after)
                assert c.physical(before) == c.physical(after)
                print('PASS quiet display registration state', action, count, flush=True)
            f.screenshot('quiet')
    finally:
        c.restore()
    state = c.command('finish')
    assert state['listeners'] == 0
    f.wait('NATIVEAPI_DISPLAY_FINAL_PASS')
    (f.out / 'result.json').write_text(json.dumps(dict(pid=f.pid, kind=f.kind, mode=a.mode,
                                                     passed=state['passed'], failed=state['failed'])))
    print(f'NATIVEAPI_DISPLAY_{a.mode.upper()}_PASS {state}; PID {f.pid}; evidence {f.out}', flush=True)


if __name__ == '__main__':
    main()
