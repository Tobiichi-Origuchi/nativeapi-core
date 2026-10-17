"""Guarded disposable guest fixture utilities; importing this module sends no input."""

import json
import os
import re
import subprocess
import time
from pathlib import Path

from PIL import Image


class Fixture:
    def __init__(self, deveco, device, suite, tag, bundle="dev.nativeapi.smoke"):
        assert bundle in ["dev.nativeapi.smoke", "dev.nativeapi.keyboard.monitor"]
        self.bundle = bundle
        assert re.fullmatch(r"127\.0\.0\.1:\d+", device)
        suffix = ".exe" if os.name == "nt" else ""
        self.device = device
        self.hdc = deveco / f"sdk/default/openharmony/toolchains/hdc{suffix}"
        self.emulator = deveco / f"tools/emulator/Emulator{suffix}"
        assert self.guest("param", "get", "const.product.model").strip() == "emulator"
        self.kind = self.guest("param", "get", "const.product.devicetype").strip()
        assert self.kind in ["phone", "2in1"]
        instances = [
            i
            for i in json.loads(self.run(self.emulator, "-list", "-details"))
            if i["isRunning"] == "true"
        ]
        expected_instance = os.environ.get("NATIVEAPI_OHOS_INSTANCE") or (
            "NativeAPI_Phone" if self.kind == "phone" else "NativeAPI_Desktop"
        )
        assert expected_instance in (
            ["NativeAPI_Phone", "NativeAPI_Phone24"]
            if self.kind == "phone"
            else ["NativeAPI_Desktop"]
        )
        assert len(instances) == 1 and instances[0]["name"] == expected_instance
        self.instance = instances[0]
        self.tag = tag
        self.out = (
            Path(__file__).resolve().parent
            / "test-results"
            / f"{suite}-suite"
            / f"{self.kind}-{time.time_ns()}"
        )
        self.out.mkdir(parents=True)
        metadata = self.guest("bm", "dump", "-n", self.bundle)
        (self.out / "bundle-metadata.json").write_text(metadata)
        self.uid = json.loads(metadata[metadata.index("{") :])["applicationInfo"]["uid"]
        self.guest("aa", "force-stop", self.bundle)
        for _ in range(50):
            if not self.guest("pidof", self.bundle).strip():
                break
            time.sleep(0.1)
        else:
            raise AssertionError("Previous fixture process did not stop")
        assert "start ability successfully" in self.guest(
            "aa",
            "start",
            "-a",
            "EntryAbility",
            "-b",
            self.bundle,
            "--ps",
            "nativeapiSuite",
            suite,
        )
        self.pid = ""
        for _ in range(50):
            self.pid = self.guest("pidof", self.bundle).strip()
            if re.fullmatch(r"\d+", self.pid):
                break
            time.sleep(0.1)
        assert re.fullmatch(r"\d+", self.pid)

    @staticmethod
    def run(*cmd):
        return subprocess.check_output(
            list(map(str, cmd)),
            stdin=subprocess.DEVNULL,
            text=True,
            encoding="utf-8",
            stderr=subprocess.STDOUT,
            timeout=35,
        )

    def guest(self, *cmd):
        return self.run(self.hdc, "-t", self.device, "shell", *cmd)

    def identity(self):
        assert self.guest("pidof", self.bundle).strip() == self.pid
        assert (
            self.guest("cat", f"/proc/{self.pid}/cmdline").split("\0")[0]
            == self.bundle
        )
        found = re.search(
            r"^Uid:\s+(\d+)\s+(\d+)\s+(\d+)\s+(\d+)",
            self.guest("cat", f"/proc/{self.pid}/status"),
            re.M,
        )
        assert found and all(int(v) == self.uid for v in found.groups())

    def logs(self):
        self.identity()
        records = self.guest("hilog", "-x", "-P", self.pid)
        (self.out / "native.log").write_text(records)
        assert not re.search(r"\bFAIL\b|NATIVEAPI_" + self.tag + r"_ERROR", records), (
            records[-6500:]
        )
        return records

    def wait(self, marker):
        deadline = time.monotonic() + 35
        while time.monotonic() < deadline:
            records = self.logs()
            if marker in records:
                return records
            time.sleep(0.15)
        raise AssertionError("Missing " + marker)

    def tree(self, phase):
        self.identity()
        remote = "/data/local/tmp/nativeapi-fixture-ui.json"
        assert "DumpLayout saved" in self.guest("uitest", "dumpLayout", "-p", remote)
        dest = self.out / f"{phase}-{time.time_ns()}.json"
        self.run(self.hdc, "-t", self.device, "file", "recv", remote, dest)
        return json.loads(dest.read_text())

    @staticmethod
    def walk(n):
        yield n
        for child in n.get("children", []):
            yield from Fixture.walk(child)

    @staticmethod
    def rect(n):
        return list(map(int, re.findall(r"-?\d+", n["attributes"]["bounds"])))

    def windows(self, phase):
        records = self.guest("hidumper", "-s", "WindowManagerService", "-a", "-a")
        (self.out / f"{phase}-windows.txt").write_text(records)
        rows = []
        for line in records.splitlines():
            match = re.match(
                r"^(.+?)\s+(\d+)\s+("
                + self.pid
                + r")\s+(\d+)\s+(\d+)\s+(\d+)\s+(\d+)\s+(-?\d+)\s+",
                line,
            )
            if match:
                rows.append(match.groups())
        return {int(row[3]): row for row in rows}

    def click(self, identifier, phase):
        t = self.tree(phase)
        matches = [
            (r, n)
            for r in t["children"]
            if r["attributes"].get("bundleName") == self.bundle
            for n in self.walk(r)
            if n["attributes"].get("id") == identifier
        ]
        assert len(matches) == 1
        root, node = matches[0]
        left, top, right, bottom = self.rect(node)
        assert (
            right > left
            and bottom > top
            and node["attributes"].get("enabled") == "true"
        )
        x, y = (left + right) // 2, (top + bottom) // 2
        hits = [
            r
            for r in t["children"]
            if self.rect(r)[0] <= x < self.rect(r)[2]
            and self.rect(r)[1] <= y < self.rect(r)[3]
        ]
        assert (
            len(hits) == 1
            and hits[0]["attributes"]["hostWindowId"]
            == root["attributes"]["hostWindowId"]
        )
        row = self.windows(phase)[int(root["attributes"]["hostWindowId"])]
        assert row[2] == self.pid and int(row[7]) > 0
        self.identity()
        (self.out / f"{phase}-owner.json").write_text(
            json.dumps(
                dict(
                    pid=self.pid,
                    uid=self.uid,
                    point=[x, y],
                    window=row[3],
                    node=node["attributes"],
                )
            )
        )
        result = self.run(
            self.emulator, "-instance", self.instance["name"], "-click", f"{x} {y}"
        )
        assert "Scenario simulation success" in result, result

    def screenshot(self, phase):
        folder = self.out / f"{phase}-screenshot"
        folder.mkdir()
        result = self.run(
            self.emulator,
            "-instance",
            self.instance["name"],
            "-screenshot",
            "-screenshotPath",
            folder,
        )
        deadline = time.monotonic() + 15
        while time.monotonic() < deadline:
            paths = list(folder.glob("*.png"))
            assert len(paths) <= 1
            if paths:
                try:
                    return Image.open(paths[0]).convert("RGB")
                except OSError:
                    pass
            time.sleep(0.1)
        raise AssertionError(result)
