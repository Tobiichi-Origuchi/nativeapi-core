#!/usr/bin/env python3
"""Run real tray registration on desktop, and service absence checks on phone."""

import argparse
import json
import os
import re
import subprocess
import sys
import time
from pathlib import Path

p = argparse.ArgumentParser()
p.add_argument("deveco", type=Path)
p.add_argument("--device", default="127.0.0.1:5555")
a = p.parse_args()
assert re.fullmatch(r"127\.0\.0\.1:\d+", a.device)
suffix = ".exe" if os.name == "nt" else ""
hdc = a.deveco / f"sdk/default/openharmony/toolchains/hdc{suffix}"
emulator = a.deveco / f"tools/emulator/Emulator{suffix}"
root = Path(__file__).resolve().parent
if os.environ.get("NATIVEAPI_OHOS_DESKTOP") == "1":
    import PIL.Image  # noqa: F401


def run(*cmd, timeout=40):
    return subprocess.check_output(
        list(map(str, cmd)),
        stdin=subprocess.DEVNULL,
        text=True,
        encoding="utf-8",
        stderr=subprocess.STDOUT,
        timeout=timeout,
    )


def guest(*cmd):
    return run(hdc, "-t", a.device, "shell", *cmd)


assert guest("param", "get", "const.product.model").strip() == "emulator"
kind = guest("param", "get", "const.product.devicetype").strip()
assert kind in ["phone", "2in1"]
instances = [
    i
    for i in json.loads(run(emulator, "-list", "-details"))
    if i["isRunning"] == "true"
]
assert len(instances) == 1 and instances[0]["name"] == (
    "NativeAPI_Phone" if kind == "phone" else "NativeAPI_Desktop"
)
out = root / "test-results/tray-suite" / f"{kind}-{time.time_ns()}"
out.mkdir(parents=True)
guest("aa", "force-stop", "dev.nativeapi.smoke")
for _ in range(50):
    if not guest("pidof", "dev.nativeapi.smoke").strip():
        break
    time.sleep(0.1)
else:
    raise AssertionError("Previous fixture process did not stop")
assert "start ability successfully" in guest(
    "aa",
    "start",
    "-a",
    "EntryAbility",
    "-b",
    "dev.nativeapi.smoke",
    "--ps",
    "nativeapiSuite",
    "tray",
)
pid = ""
for _ in range(50):
    pid = guest("pidof", "dev.nativeapi.smoke").strip()
    if re.fullmatch(r"\d+", pid):
        break
    time.sleep(0.1)
assert re.fullmatch(r"\d+", pid)
if kind == "2in1":
    try:
        result = run(
            sys.executable,
            "-u",
            root / "verify_tray_ui.py",
            a.deveco,
            "--device",
            a.device,
            "--pid",
            pid,
            timeout=240,
        )
    except subprocess.CalledProcessError as error:
        (out / "ui.log").write_text(error.output)
        print(error.output, flush=True)
        raise
    (out / "ui.log").write_text(result)
    print(result)
else:
    for _ in range(100):
        records = guest("hilog", "-x", "-P", pid, "-T", "nativeapi-tray")
        assert not re.search(r"\bFAIL\b", records), records
        if "NATIVEAPI_TRAY_FINAL_PASS" in records:
            break
        time.sleep(0.2)
    else:
        raise AssertionError("Phone tray suite did not finish")
    assert (
        "phone rejects unavailable desktop tray service" in records
        and '"supported":false' in records
    )
records = guest("hilog", "-x", "-P", pid)
(out / "native.log").write_text(records)
assert not re.search(r"\bFAIL\b|NATIVEAPI_TRAY_(ERROR|TIMEOUT)", records), records[
    -8000:
]
assert "NATIVEAPI_TRAY_FINAL_PASS" in records
print(f"NATIVEAPI_TRAY_SUITE_PASS kind={kind} pid={pid} evidence={out}")
