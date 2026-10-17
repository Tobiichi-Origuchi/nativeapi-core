#!/usr/bin/env python3
"""Live normal-app device metadata, independent SDK/ELF, and restored name edits.

No mouse or keyboard input. Only a separately signed fixture writes DEVICE_NAME
through the public Settings API; its original is persisted before mutation.
"""

import argparse
import json
import re
import time
from pathlib import Path

from ohos_ui_fixture import Fixture

WRITER = "dev.nativeapi.deviceinfo.writer"


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("deveco", type=Path)
    parser.add_argument("--device", default="127.0.0.1:5555")
    args = parser.parse_args()
    f = Fixture(args.deveco, args.device, "device-info", "DEVICE")
    assert f.instance["name"] in ["NativeAPI_Phone", "NativeAPI_Desktop"]
    root = f.out
    states = []
    sequence = 0
    cleanup = None

    def record(request, label):
        f.identity()
        marker = f"NATIVEAPI_DEVICE_REPLY {request} "
        log = f.wait(marker)
        result = json.loads(
            next(
                line.split(marker, 1)[1] for line in log.splitlines() if marker in line
            )
        )
        ref_marker = f"NATIVEAPI_DEVICE_REFERENCE {request} "
        reference = json.loads(
            next(
                line.split(ref_marker, 1)[1]
                for line in log.splitlines()
                if ref_marker in line
            )
        )
        assert result["failed"] == 0 and result["passed"] == 177, result
        assert result["samples"] == 20 and result["faultsPassed"] == 39, result
        assert reference["settingsError"] == 0, reference
        expected = [
            reference[k]
            for k in ["name", "model", "manufacturer", "osName", "osVersion"]
        ]
        kernel = f.guest("uname", "-r").strip()
        # The loader supplies the actual loaded native library's ELF identity;
        # no permission to inspect another process or a kernel ABI guess is used.
        elf = bytes(result["elf"])
        elf_text = " ".join(map(str, elf))
        assert elf[:4] == b"\x7fELF" and len(elf) == 20 and elf[5] == 1
        assert result["pointerBits"] == (64 if elf[4] == 2 else 32)
        machine = int.from_bytes(elf[18:20], "little")
        architecture = {
            (183, 2): "aarch64",
            (62, 2): "x86_64",
            (40, 1): "arm",
            (3, 1): "i686",
        }[(machine, elf[4])]
        expected += [kernel, architecture]
        assert result["cpp"] == result["c"] == result["worker"] == expected, (
            result,
            expected,
        )
        assert (
            result["pre"] == result["preC"] == [reference["marketName"], *expected[1:]]
        )
        metadata = f.guest("bm", "dump", "-n", f.bundle)
        data = json.loads(metadata[metadata.index("{") :])
        assert not data["applicationInfo"]["isSystemApp"]
        assert "ohos.permission.MANAGE_SETTINGS" not in data["reqPermissions"]
        state = dict(
            label=label,
            pid=f.pid,
            uid=f.uid,
            native=result,
            sdk=reference,
            elfClass=elf[4],
            elfMachine=machine,
        )
        (root / f"{label}.json").write_text(json.dumps(state, indent=2))
        (root / f"{label}.log").write_text(log)
        (root / f"{label}-elf.txt").write_text(elf_text)
        print(label, state, flush=True)
        return state

    def refresh(label):
        request = str(time.time_ns())
        assert "start ability successfully" in f.guest(
            "aa",
            "start",
            "-a",
            "EntryAbility",
            "-b",
            f.bundle,
            "--ps",
            "nativeapiSuite",
            "device-info",
            "--ps",
            "nativeapiDeviceRequest",
            request,
        )
        return record(request, label)

    def writer(command):
        nonlocal sequence
        f.identity()
        metadata = f.guest("bm", "dump", "-n", WRITER)
        data = json.loads(metadata[metadata.index("{") :])
        info = data["applicationInfo"]
        assert info["isSystemApp"] and info["appPrivilegeLevel"] == "system_basic"
        assert data["reqPermissions"] == ["ohos.permission.MANAGE_SETTINGS"]
        assert info["uid"] != f.uid
        request = str(time.time_ns())
        assert "start ability successfully" in f.guest(
            "aa",
            "start",
            "-a",
            "EntryAbility",
            "-b",
            WRITER,
            "--ps",
            "nativeapiSuite",
            "device-info-writer",
            "--ps",
            "nativeapiDeviceRequest",
            request,
            "--ps",
            "nativeapiDeviceCommand",
            command,
        )
        marker = f"NATIVEAPI_DEVICE_WRITER_REPLY {request} {command} "
        deadline = time.monotonic() + 30
        while time.monotonic() < deadline:
            pid = f.guest("pidof", WRITER).strip()
            if not re.fullmatch(r"\d+", pid):
                time.sleep(0.1)
                continue
            assert f.guest("cat", f"/proc/{pid}/cmdline").split("\0")[0] == WRITER
            status = f.guest("cat", f"/proc/{pid}/status")
            uid = re.search(r"^Uid:\s+(\d+)\s+(\d+)\s+(\d+)\s+(\d+)", status, re.M)
            assert uid and all(int(n) == info["uid"] for n in uid.groups())
            log = f.guest("hilog", "-x", "-P", pid)
            assert f"NATIVEAPI_DEVICE_WRITER_ERROR {request}" not in log, log[-6000:]
            rows = [
                line.split(marker, 1)[1] for line in log.splitlines() if marker in line
            ]
            if rows:
                reply = json.loads(rows[-1])
                sequence += 1
                (root / f"writer-{sequence:02d}-{command}.json").write_text(
                    json.dumps(dict(pid=pid, uid=info["uid"], reply=reply), indent=2)
                )
                (root / f"writer-{sequence:02d}-{command}.log").write_text(log)
                return pid, reply
            time.sleep(0.1)
        raise AssertionError("Missing writer reply: " + command)

    states.append(record("initial", "initial"))
    original = None
    try:
        writer_pid, prepared = writer("prepare")
        original = prepared["original"]
        assert prepared["active"] and prepared["name"] == original
        baseline = refresh("prepared")
        assert baseline["sdk"]["settingsName"] == original
        states.append(baseline)
        for command, label in [("set-a", "name-a"), ("set-b", "name-b")]:
            pid, changed = writer(command)
            assert (
                pid == writer_pid and changed["active"] and changed["name"] != original
            )
            observed = refresh(label)
            assert observed["sdk"]["name"] == changed["name"]
            assert observed["native"]["cpp"][0] != observed["sdk"]["marketName"]
            states.append(observed)
        # A fresh reader must still return the live name, with NDK fallback only
        # before its UI host/context attaches.
        old_reader = f.pid
        f = Fixture(args.deveco, args.device, "device-info", "DEVICE")
        assert f.pid != old_reader
        restarted = record("initial", "reader-restarted")
        assert restarted["sdk"]["name"] == states[-1]["sdk"]["name"]
        states.append(restarted)
        # Restart the privileged writer too, proving restoration uses its saved
        # original rather than a current in-memory copy of the changed name.
        f.guest("aa", "force-stop", WRITER)
        restored_pid, restored = writer("restore")
        assert (
            restored_pid != writer_pid
            and not restored["active"]
            and restored["name"] == original
        )
        final = refresh("restored")
        assert final["sdk"]["settingsName"] == original
        states.append(final)
    finally:
        _, restored = writer("restore")
        cleanup = refresh("finally-restored")
        assert not restored["active"] and (
            original is None or restored["name"] == original
        )
        assert cleanup["sdk"]["settingsName"] == restored["name"]
    report = dict(
        guest=f.instance["name"],
        readerPids=sorted({s["pid"] for s in states}),
        phases=len(states),
        nativeAssertions=sum(s["native"]["passed"] for s in states),
        faultAssertions=39 * len({s["pid"] for s in states}),
        samples=sum(s["native"]["samples"] for s in states),
        original=original,
        restored=cleanup["sdk"]["settingsName"],
        failed=0,
        evidence=str(root),
        restartedReaderEvidence=str(f.out),
    )
    (root / "summary.json").write_text(json.dumps(report, indent=2))
    print("PASS", json.dumps(report), flush=True)


if __name__ == "__main__":
    main()
