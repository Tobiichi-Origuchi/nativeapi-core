#!/usr/bin/env python3
"""Drive the real system document picker in a disposable named HarmonyOS guest."""
import argparse
import json
import os
import re
import subprocess
import time
from pathlib import Path


class Guest:
    begin_marker = 'NATIVEAPI_PICKER_BEGIN'

    def __init__(self, deveco, device, pid):
        assert re.fullmatch(r'127\.0\.0\.1:\d+', device)
        suffix = '.exe' if os.name == 'nt' else ''
        self.hdc = deveco / f'sdk/default/openharmony/toolchains/hdc{suffix}'
        self.emulator = deveco / f'tools/emulator/Emulator{suffix}'
        self.device, self.pid = device, pid
        self.out = Path(__file__).resolve().parent / 'test-results/picker-ui' / str(pid)
        self.out.mkdir(parents=True, exist_ok=True)
        self.sequence = 0
        self.phase = None
        self.run(self.hdc, 'tconn', device)
        assert self.shell('param', 'get', 'const.product.model').strip() == 'emulator'
        instances = json.loads(self.run(self.emulator, '-list', '-details'))
        running = [x for x in instances if x['isRunning'] == 'true']
        assert len(running) == 1 and running[0]['name'].startswith('NativeAPI_')
        self.instance = running[0]
        self.desktop = self.shell('param', 'get', 'const.product.devicetype').strip() == '2in1'
        assert self.instance['deviceType'] == ('2in1' if self.desktop else 'phone')
        self.alive()

    @staticmethod
    def run(*command):
        return subprocess.check_output([str(x) for x in command], text=True, encoding='utf-8',
                                       stderr=subprocess.STDOUT, stdin=subprocess.DEVNULL, timeout=25)

    def shell(self, *command):
        return self.run(self.hdc, '-t', self.device, 'shell', *command)

    def alive(self):
        assert int(self.shell('pidof', 'dev.nativeapi.smoke').strip()) == self.pid

    def log(self):
        text = self.shell('hilog', '-x', '-T', 'nativeapi-picker', '-P', self.pid)
        (self.out / 'runtime.log').write_text(text, encoding='utf-8')
        assert not re.search(r'NATIVEAPI_PICKER_(?:CANCEL_)?FAIL', text), text[-4000:]
        return text

    def layout(self, label):
        self.alive()
        status = self.run(self.emulator, '-instance', self.instance['name'], '-uiLayout', '-i')
        assert 'Scenario simulation success' in status, status
        text = (Path(self.instance['instancePath']) / 'uiLayout/analysis.md').read_text(encoding='utf-8')
        self.sequence += 1
        (self.out / f'{self.sequence:03d}-{label}-layout.md').write_text(text, encoding='utf-8')
        return text

    @staticmethod
    def widgets(text, pattern):
        regex = pattern + r' \[id:(\d+)\] \[top: (\d+), left: (\d+), width: (\d+), height: (\d+)\]'
        return [list(map(int, row)) for row in re.findall(regex, text)]

    def phase_ready(self, label, pattern):
        self.phase = label
        for _ in range(80):
            log = self.log()
            if f'NATIVEAPI_PICKER_BEGIN {label} ' in log:
                text = self.layout(label)
                if self.widgets(text, pattern):
                    return text
            time.sleep(.2)
        raise AssertionError(f'System picker did not reach {label}')

    def tree(self, label):
        self.sequence += 1
        remote = '/data/local/tmp/nativeapi-picker-tree.json'
        local = self.out / f'{self.sequence:03d}-{label}.json'
        for _ in range(2):
            self.alive()
            status = self.shell('uitest', 'dumpLayout', '-p', remote)
            if 'DumpLayout saved' in status:
                self.run(self.hdc, '-t', self.device, 'file', 'recv', remote, local)
                data = json.loads(local.read_text(encoding='utf-8'))
                if data.get('children'):
                    return data
            time.sleep(.2)
        raise AssertionError('Native UI ownership snapshot unavailable')

    @staticmethod
    def descendants(node):
        yield node
        for child in node.get('children', []):
            yield from Guest.descendants(child)

    @staticmethod
    def rectangle(attributes):
        match = re.fullmatch(r'\[(-?\d+),(-?\d+)\]\[(-?\d+),(-?\d+)\]', attributes.get('bounds', ''))
        assert match
        return list(map(int, match.groups()))

    def guard(self, x, y, system):
        self.alive()
        if system:
            phases = re.findall(re.escape(self.begin_marker) + r' (\S+) ', self.log())
            assert phases and phases[-1] == self.phase, 'Native picker phase changed before the press'
        data = self.tree('ownership')
        hits = []
        for root in data['children']:
            a = root['attributes']
            match = re.fullmatch(r'\[(-?\d+),(-?\d+)\]\[(-?\d+),(-?\d+)\]', a['bounds'])
            assert match
            left, top, right, bottom = map(int, match.groups())
            if left <= x < right and top <= y < bottom:
                hits.append(a)
        bundle = 'com.huawei.hmos.filemanager' if system and self.desktop else 'dev.nativeapi.smoke'
        assert len(hits) == 1 and hits[0].get('bundleName') == bundle, f'Unsafe press owner: {hits}'
        windows = self.shell('hidumper', '-s', 'WindowManagerService', '-a', '-a')
        (self.out / f'{self.sequence:03d}-windows.txt').write_text(windows, encoding='utf-8')
        identifier = re.escape(hits[0]['hostWindowId'])
        if system and self.desktop:
            # The SDK's file-manager UIExtension window is explicitly hosted by
            # this test app. A separate file-manager application is not allowed.
            pattern = r'^dev\.nativeapi\.smoke\S*\s+\d+\s+\d+\s+' + identifier + r'\s+1001\s+'
        else:
            pattern = r'^\S+\s+\d+\s+' + str(self.pid) + r'\s+' + identifier + r'\s+'
        assert re.search(pattern, windows, re.M), 'Native window is not the expected app/hosted picker'
        if system and not self.desktop:
            # Phone pickers are embedded inside our native app window. Verify
            # that the press lies in its modal UIExtension, not the page behind it.
            extensions = []
            for node in self.descendants(data):
                a = node.get('attributes', {})
                if a.get('type') == 'UIExtensionComponent' and a.get('hostWindowId') == hits[0]['hostWindowId']:
                    left, top, right, bottom = self.rectangle(a)
                    if left <= x < right and top <= y < bottom:
                        extensions.append(node)
            assert len(extensions) == 1, 'Press must belong to the test app modal picker extension'

    def press(self, row, system=True):
        identifier, top, left, width, height = row
        assert width > 0 and height > 0
        self.guard(left + width // 2, top + height // 2, system)
        self.run(self.emulator, '-instance', self.instance['name'], '-click', identifier)
        time.sleep(.35)

    def click(self, pattern, system=True, leftmost=False):
        text = self.layout(self.phase or 'start')
        rows = self.widgets(text, pattern)
        assert rows and (len(rows) == 1 or leftmost), f'Widget missing/ambiguous: {pattern}'
        self.press(min(rows, key=lambda row: row[2]), system)

    def click_id(self, identifier):
        tree = self.tree('native-id')
        matches = [n['attributes'] for n in self.descendants(tree) if n.get('attributes', {}).get('id') == identifier]
        assert len(matches) == 1, f'Native picker control missing: {identifier}'
        left, top, right, bottom = self.rectangle(matches[0])
        assert right > left and bottom > top
        x, y = (left + right) // 2, (top + bottom) // 2
        self.guard(x, y, True)
        self.run(self.emulator, '-instance', self.instance['name'], '-click', f'{x} {y}')
        time.sleep(.35)

    def downloads(self):
        if self.desktop:
            self.click(r'Text 下载', leftmost=True)
            return
        text = self.layout('phone-navigation')
        if 'Text 将文件保存至 "Download" ' in text:
            return  # The native save sheet retained the previously chosen folder.
        if self.widgets(text, r'Text 知道了'):
            self.click(r'Text 知道了')
            text = self.layout('phone-notice-dismissed')
        target = r'(?:Text|Row (?:symbol|image)) Download'
        if not self.widgets(text, target):
            if self.widgets(text, r'Text 浏览'):
                self.click(r'Text 浏览')
            entered_storage = False
            for _ in range(20):
                text = self.layout('phone-folders')
                if self.widgets(text, target):
                    break
                # The provider remembers its last location. Browse can already
                # show the directory list, or load the storage card later.
                if not entered_storage and self.widgets(text, r'Text 我的手机'):
                    self.click(r'Text 我的手机')
                    entered_storage = True
                time.sleep(.15)
            else:
                raise AssertionError('Phone Download folder did not load')
        self.click(target)

    def phone_confirm(self):
        phases = re.findall(r'NATIVEAPI_PICKER_BEGIN (\S+) ', self.log())
        if phases[-1] != self.phase:
            return  # single-file selection can finish immediately
        tree = self.tree('phone-confirm')
        ids = [n.get('attributes', {}).get('id') for n in self.descendants(tree)]
        if 'dialog_confirm' in ids:
            self.click_id('dialog_confirm')
        else:
            self.click(r'Text 完成')

    def phone_cancel(self):
        for _ in range(8):
            log = self.log()
            phases = re.findall(r'NATIVEAPI_PICKER_BEGIN (\S+) ', log)
            if re.search(r'NATIVEAPI_PICKER_(?:CANCEL_)?PASS', log) or phases[-1] != self.phase:
                return
            tree = self.tree('phone-cancel')
            nodes = [n.get('attributes', {}) for n in self.descendants(tree)]
            tabs = [a for a in nodes if a.get('id') == 'SegmentButton']
            if tabs:
                left, top, right, bottom = self.rectangle(tabs[0])
                buttons = [a for a in nodes if a.get('type') == 'Button']
                candidates = []
                for button in buttons:
                    a, b, c, d = self.rectangle(button)
                    if a >= right and top <= (b + d) // 2 < bottom and c > a and d > b:
                        candidates.append((a, b, c, d))
                assert len(candidates) == 1, 'Native phone picker close control is ambiguous'
                a, b, c, d = candidates[0]
                x, y = (a + c) // 2, (b + d) // 2
                self.guard(x, y, True)
                self.run(self.emulator, '-instance', self.instance['name'], '-click', f'{x} {y}')
                # A successful dismissal can open the next test's picker while
                # a fresh UI snapshot is being collected. Never press its X.
                for _ in range(60):
                    log = self.log()
                    phases = re.findall(r'NATIVEAPI_PICKER_BEGIN (\S+) ', log)
                    if re.search(r'NATIVEAPI_PICKER_(?:CANCEL_)?PASS', log) or phases[-1] != self.phase:
                        return
                    time.sleep(.1)
                raise AssertionError('Native picker did not finish after its close control')
            elif any(a.get('id') == 'left_icon' for a in nodes):
                self.click_id('left_icon')
            else:
                time.sleep(.2)
        raise AssertionError('Phone system picker did not report cancellation')

    def document_name(self, suffix):
        # PIDs repeat across guest boots. Follow the exact current native fixture name.
        marker = rf'NATIVEAPI_PICKER_BEGIN save-{suffix} (NativeAPI-picker-{self.pid}-[^\r\n]+)'
        deadline = time.monotonic() + 25
        while time.monotonic() < deadline:
            found = re.findall(marker, self.log())
            if found:
                name = found[-1].strip()
                extension = 'md' if suffix == 'b' else 'txt'
                assert re.fullmatch(rf'NativeAPI-picker-{self.pid}-(?:\d+-)?{suffix} 中文 #%\.{extension}', name)
                return name
            time.sleep(.1)
        raise AssertionError(f'Native picker did not publish the save-{suffix} document name')

    def name(self, suffix):
        # The system's desktop file list hides known extensions; match either form.
        stem, extension = os.path.splitext(self.document_name(suffix))
        return re.escape('Text ' + stem) + '(?:' + re.escape(extension) + ')?'

    def select_file(self, suffix, multiple=False):
        if not self.desktop:
            prefix = os.path.splitext(self.document_name(suffix))[0]
            tree = self.tree('phone-select-file')
            rows = []
            for node in self.descendants(tree):
                a = node.get('attributes', {})
                if a.get('type') == 'Text' and a.get('text', '').startswith(prefix):
                    left, top, right, bottom = self.rectangle(a)
                    if right > left and bottom > top:
                        rows.append((left, top, right, bottom))
            rows = list(set(rows))
            assert len(rows) == 1, 'Owned phone document must be uniquely identifiable'
            left, top, right, bottom = rows[0]
            x, y = (left + right) // 2, (top + bottom) // 2
            self.guard(x, y, True)
            self.run(self.emulator, '-instance', self.instance['name'], '-click', f'{x} {y}')
            time.sleep(.35)
            return
        text = self.layout('select-file')
        rows = self.widgets(text, self.name(suffix))
        assert len(rows) == 1, 'The owned test document must be uniquely identifiable'
        row = rows[0]
        if multiple:
            checks = self.widgets(text, r'Checkbox')
            checks = [c for c in checks if abs((c[1] + c[4] / 2) - (row[1] + row[4] / 2)) <= max(c[4], row[4])]
            assert len(checks) == 1, 'No unambiguous native multi-select checkbox for this document'
            row = checks[0]
        self.press(row)

    def select_many_desktop(self):
        text = self.layout('box-selection-before')
        a, b = self.widgets(text, self.name('a')), self.widgets(text, self.name('b'))
        assert len(a) == len(b) == 1
        a, b = a[0], b[0]
        items = self.widgets(text, r'GridItem')
        right = max(row[2] + row[3] for row in items)
        grids = [row for row in self.widgets(text, r'Grid') if row[2] < a[2] and row[2] + row[3] > right]
        assert len(grids) == 1
        grid = grids[0]
        x1, y1 = (right + grid[2] + grid[3]) // 2, min(a[1], b[1]) - 10
        x2, y2 = min(a[2], b[2]) - 10, max(a[1] + a[4], b[1] + b[4]) + 10
        assert right < x1 < grid[2] + grid[3] and grid[1] < y1 < y2 < grid[1] + grid[4]
        self.guard(x1, y1, True)
        # One complete guest gesture starts in verified blank list space. The
        # host pointer and keyboard are never used.
        self.run(self.emulator, '-instance', self.instance['name'], '-slide', f'{x1} {y1} {x2} {y2}')
        self.layout('box-selection-after')
        remote = '/data/local/tmp/nativeapi-picker-selected.json'
        self.shell('uitest', 'dumpLayout', '-p', remote)
        local = self.out / 'selected-documents.json'
        self.run(self.hdc, '-t', self.device, 'file', 'recv', remote, local)
        selected = []
        def visit(node):
            attributes = node.get('attributes', {})
            if attributes.get('type') == 'GridItem' and attributes.get('selected') == 'true':
                selected.append(list(map(int, re.findall(r'-?\d+', attributes['bounds']))))
            for child in node.get('children', []):
                visit(child)
        visit(json.loads(local.read_text(encoding='utf-8')))
        assert len(selected) == 2, 'Native file manager must report exactly two selected rows'
        for row in [a, b]:
            x, y = row[2] + row[3] // 2, row[1] + row[4] // 2
            assert sum(left <= x < right and top <= y < bottom for left, top, right, bottom in selected) == 1


class ReferenceGuest(Guest):
    """Drive the same owned SDK modal extension using its actual reference phase."""
    begin_marker = 'SDK_REFERENCE_BEGIN'

    def __init__(self, deveco, device, pid):
        super().__init__(deveco, device, pid)
        self.out = self.out / ('sdk-' + str(time.time_ns()))
        self.out.mkdir()
        self.phase = 'reference'

    def log(self):
        self.alive()
        text = self.shell('hilog', '-x', '-P', self.pid)
        (self.out / 'runtime.log').write_text(text, encoding='utf-8')
        assert 'SDK_REFERENCE_FAILED' not in text, text[-4000:]
        return text

    def guard(self, x, y, system):
        assert 'SDK_REFERENCE_RESULT' not in self.log(), 'SDK picker already completed'
        return super().guard(x, y, system)


def verify_completed(guest, pass_marker):
    # The SDK may leave another application's window focused after dismissing
    # its last picker. Read this process's actual UI tree, not the emulator's
    # focused-window-only inspector (which may be Settings or a browser).
    for _ in range(80):
        if pass_marker in guest.log():
            data = guest.tree('finished-report')
            roots = [r for r in data['children'] if r['attributes'].get('bundleName') == 'dev.nativeapi.smoke']
            text = '\n'.join(n['attributes'].get('text', '') for r in roots for n in guest.descendants(r))
            windows = guest.shell('hidumper', '-s', 'WindowManagerService', '-a', '-a')
            (guest.out / 'finished-report-windows.txt').write_text(windows, encoding='utf-8')
            if roots and re.search(pass_marker + r' \(\d+ passed, 0 failed\)', text):
                for root in roots:
                    wid = re.escape(root['attributes']['hostWindowId'])
                    assert re.search(r'^\S+\s+\d+\s+' + str(guest.pid) + r'\s+' + wid + r'\s+', windows, re.M), 'Report root belongs to another process'
                extensions = [n for r in roots for n in guest.descendants(r) if n['attributes'].get('type') == 'UIExtensionComponent']
                hosted = re.findall(r'^dev\.nativeapi\.smoke\S*\s+\d+\s+\d+\s+\d+\s+1001\s+\d+\s+\d+\s+(-?\d+)\s+', windows, re.M)
                if not extensions and not any(int(z) > 0 for z in hosted):
                    return
        time.sleep(.2)
    raise AssertionError('Native picker report is not visible on its own live window or a modal picker remains')


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('deveco', type=Path)
    parser.add_argument('--device', default='127.0.0.1:5555')
    parser.add_argument('--pid', type=int, required=True)
    parser.add_argument('--cancel-only', action='store_true')
    parser.add_argument('--resume', choices=['save-a', 'save-b', 'open-one', 'open-many', 'folder', 'cancel', 'c-cancel'])
    args = parser.parse_args()
    guest = Guest(args.deveco, args.device, args.pid)
    if not args.resume:
        for _ in range(50):
            text = guest.layout('start')
            if 'NativeAPI system picker ready' in text or 'NATIVEAPI_OHOS_PASS' in text:
                break
            time.sleep(.2)
        else:
            raise AssertionError('Native test host did not become ready')
        guest.click(r'Button Run native file picker tests', system=False)
    labels = ['save-a', 'save-b', 'open-one', 'open-many', 'folder', 'cancel', 'c-cancel']
    if args.cancel_only:
        labels = ['cancel', 'c-cancel']
    first = labels.index(args.resume) if args.resume else 0
    for label in labels[first:]:
        if label.startswith('save-'):
            suffix = 'b' if label == 'save-b' else 'a'
            pattern = r'(?:TextInput|Text) ' + re.escape(guest.document_name(suffix))
        else:
            pattern = r'(?:Button|Text) 取消' if guest.desktop else (r'Text 浏览' if label != 'folder' else r'(?:Column|Text 浏览)')
        text = guest.phase_ready(label, pattern)
        if label.startswith('save-'):
            assert ('.md' if label == 'save-b' else '.txt') in text
            guest.downloads()
            guest.click(r'Button 保存') if guest.desktop else guest.phone_confirm()
        elif label in ['open-one', 'open-many']:
            guest.downloads()
            if label == 'open-many' and guest.desktop:
                guest.select_many_desktop()
            else:
                guest.select_file('a', label == 'open-many')
                if label == 'open-many':
                    guest.select_file('b', True)
            guest.click(r'Button 打开') if guest.desktop else guest.phone_confirm()
        elif label == 'folder':
            guest.downloads()
            if not guest.desktop:
                guest.phone_confirm()
                print(f'PASS guarded system picker interaction: {label}', flush=True)
                continue
            text = guest.layout('folder-confirm')
            patterns = [r'Button 选择文件夹', r'Button 选择此文件夹', r'Button 选择']
            available = [p for p in patterns if guest.widgets(text, p)]
            assert len(available) == 1, 'System folder acceptance control missing'
            guest.click(available[0])
        else:
            guest.click(r'(?:Button|Text) 取消') if guest.desktop else guest.phone_cancel()
        print(f'PASS guarded system picker interaction: {label}', flush=True)
    pass_marker = 'NATIVEAPI_PICKER_CANCEL_PASS' if args.cancel_only else 'NATIVEAPI_PICKER_PASS'
    verify_completed(guest, pass_marker)
    ui_marker = 'NATIVEAPI_PICKER_CANCEL_UI_PASS' if args.cancel_only else 'NATIVEAPI_PICKER_UI_PASS'
    print(f'{ui_marker} ({len(labels) - first + 1} passed, 0 failed)', flush=True)



if __name__ == '__main__':
    main()
