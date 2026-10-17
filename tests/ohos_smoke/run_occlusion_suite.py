#!/usr/bin/env python3
"""Prove real compositor occlusion, public SDK/C++/C state and native events."""

import argparse
import json
import os
import re
import subprocess
import time
from pathlib import Path

from PIL import Image

p = argparse.ArgumentParser()
p.add_argument("deveco", type=Path)
p.add_argument("--device", default="127.0.0.1:5555")
a = p.parse_args()
assert re.fullmatch(r"127\.0\.0\.1:\d+", a.device)
suffix = ".exe" if os.name == "nt" else ""
hdc = a.deveco / f"sdk/default/openharmony/toolchains/hdc{suffix}"
emulator = a.deveco / f"tools/emulator/Emulator{suffix}"


def run(*cmd, timeout=35):
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
instance = instances[0]
out = (
    Path(__file__).resolve().parent
    / "test-results/occlusion-suite"
    / f"{kind}-{time.time_ns()}"
)
out.mkdir(parents=True)
metadata = guest("bm", "dump", "-n", "dev.nativeapi.smoke")
uid = json.loads(metadata[metadata.index("{") :])["applicationInfo"]["uid"]
guest("aa", "force-stop", "dev.nativeapi.smoke")
for _ in range(50):
    if not guest("pidof", "dev.nativeapi.smoke").strip():
        break
    time.sleep(0.1)
else:
    raise AssertionError("Previous process did not stop")
assert "start ability successfully" in guest(
    "aa",
    "start",
    "-a",
    "EntryAbility",
    "-b",
    "dev.nativeapi.smoke",
    "--ps",
    "nativeapiSuite",
    "occlusion",
)
app_pid = ""
for _ in range(50):
    app_pid = guest("pidof", "dev.nativeapi.smoke").strip()
    if re.fullmatch(r"\d+", app_pid):
        break
    time.sleep(0.1)
assert re.fullmatch(r"\d+", app_pid)


def identity():
    assert guest("pidof", "dev.nativeapi.smoke").strip() == app_pid
    assert (
        guest("cat", f"/proc/{app_pid}/cmdline").split("\0")[0] == "dev.nativeapi.smoke"
    )
    found = re.search(
        r"^Uid:\s+(\d+)\s+(\d+)\s+(\d+)\s+(\d+)",
        guest("cat", f"/proc/{app_pid}/status"),
        re.M,
    )
    assert found and all(int(v) == uid for v in found.groups())


def logs():
    identity()
    records = guest("hilog", "-x", "-P", app_pid)
    (out / "native.log").write_text(records)
    assert not re.search(r"\bFAIL\b|NATIVEAPI_OCCLUSION_ERROR", records), records[
        -6000:
    ]
    return records


def wait(marker):
    deadline = time.monotonic() + 30
    while time.monotonic() < deadline:
        records = logs()
        if marker in records:
            return records
        time.sleep(0.15)
    raise AssertionError("Missing " + marker)


def tree(phase):
    identity()
    remote = "/data/local/tmp/nativeapi-occlusion.json"
    assert "DumpLayout saved" in guest("uitest", "dumpLayout", "-p", remote)
    dest = out / f"{phase}-{time.time_ns()}.json"
    run(hdc, "-t", a.device, "file", "recv", remote, dest)
    return json.loads(dest.read_text())


def walk(n):
    yield n
    for child in n.get("children", []):
        yield from walk(child)


def rect(n):
    return list(map(int, re.findall(r"-?\d+", n["attributes"]["bounds"])))


def windows(phase):
    native = guest("hidumper", "-s", "WindowManagerService", "-a", "-a")
    (out / f"{phase}-windows.txt").write_text(native)
    rows = []
    for line in native.splitlines():
        match = re.match(
            r"^(.+?)\s+(\d+)\s+("
            + app_pid
            + r")\s+(\d+)\s+(\d+)\s+(\d+)\s+(\d+)\s+(-?\d+)\s+",
            line,
        )
        if match:
            rows.append(match.groups())
    return {int(row[3]): row for row in rows}


def click(identifier, phase):
    t = tree(phase)
    matches = [
        (r, n)
        for r in t["children"]
        if r["attributes"].get("bundleName") == "dev.nativeapi.smoke"
        for n in walk(r)
        if n["attributes"].get("id") == identifier
    ]
    assert len(matches) == 1
    root, node = matches[0]
    left, top, right, bottom = rect(node)
    assert right > left and bottom > top and node["attributes"].get("enabled") == "true"
    x, y = (left + right) // 2, (top + bottom) // 2
    hits = [
        r
        for r in t["children"]
        if rect(r)[0] <= x < rect(r)[2] and rect(r)[1] <= y < rect(r)[3]
    ]
    assert (
        len(hits) == 1
        and hits[0]["attributes"]["hostWindowId"] == root["attributes"]["hostWindowId"]
    )
    row = windows(phase)[int(root["attributes"]["hostWindowId"])]
    assert row[2] == app_pid and int(row[7]) > 0
    identity()
    (out / f"{phase}-input-owner.json").write_text(
        json.dumps(
            dict(
                pid=app_pid,
                uid=uid,
                window=row[3],
                point=[x, y],
                node=node["attributes"],
            )
        )
    )
    result = run(emulator, "-instance", instance["name"], "-click", f"{x} {y}")
    assert "Scenario simulation success" in result, result


def screenshot(phase):
    folder = out / f"{phase}-screenshot"
    folder.mkdir()
    result = run(
        emulator,
        "-instance",
        instance["name"],
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


def verify(index, phase, records):
    line = next(
        line
        for line in records.splitlines()
        if f"NATIVEAPI_OCCLUSION_PHASE {index} {phase} " in line
    )
    matched = re.search(
        r"sdk=(\d+) native=(\{.*\}) target=(\{.*\}) cover=(\{.*\})", line
    )
    assert matched, line
    sdk = int(matched[1])
    state, target, cover = map(json.loads, matched.groups()[1:])
    assert state["failed"] == 0 and state["state"] == (2 if sdk == 2 else 1)
    rows = windows(f"{index}-{phase}")
    tr, cr = rows[target["id"]], rows.get(cover["id"])
    assert tr[2] == app_pid and (cr is None or cr[2] == app_pid)
    tree(f"{index}-{phase}-scene")
    if phase == "hidden":
        assert int(tr[7]) < 0 and (cr is None or int(cr[7]) < 0)
    else:
        assert int(tr[7]) > 0
        if phase in ["uncovered", "reshown", "unobserved-visible"]:
            assert cr is None or int(cr[7]) < 0
        else:
            assert cr is not None and int(cr[7]) > int(tr[7])
        r, c = target["windowRect"], cover["windowRect"]
        if "full" in phase or phase == "transparent":
            assert c["left"] <= r["left"] and c["top"] <= r["top"]
            assert c["left"] + c["width"] >= r["left"] + r["width"]
            assert c["top"] + c["height"] >= r["top"] + r["height"]
        picture = screenshot(f"{index}-{phase}")
        colors = []
        for fraction in [0.25, 0.75]:
            x, y = (
                int(r["left"] + r["width"] * fraction),
                int(r["top"] + r["height"] * 0.2),
            )
            assert 0 <= x < picture.width and 0 <= y < picture.height
            samples = [
                picture.getpixel((x + dx, y + dy))
                for dx in [-1, 0, 1]
                for dy in [-1, 0, 1]
            ]
            if "full" in phase:
                expected = (0, 255, 255)
            elif phase == "transparent":
                expected = (127, 128, 255)
            elif phase == "partial" and fraction == 0.75:
                expected = (0, 255, 255)
            else:
                expected = (255, 0, 255)
            assert all(
                all(abs(pixel[i] - expected[i]) <= 8 for i in range(3))
                for pixel in samples
            ), (phase, [x, y], samples, expected)
            colors.append(dict(point=[x, y], pixels=samples, expected=expected))
        (out / f"{index}-{phase}-pixels.json").write_text(json.dumps(colors))
    print(
        "PASS actual native scene, SDK/reference/worker/C++/C events",
        index,
        phase,
        flush=True,
    )


wait("NATIVEAPI_OCCLUSION_READY")
click("nativeapi-occlusion-run", "start")
for index, phase in [
    (0, "uncovered"),
    (1, "partial"),
    (2, "full"),
    (3, "transparent"),
    (4, "full-again"),
    (5, "uncovered"),
    (6, "hidden"),
    (7, "reshown"),
    (9, "unobserved-full"),
    (10, "unobserved-visible"),
    (12, "hidden"),
    (13, "reshown"),
]:
    records = wait(f"NATIVEAPI_OCCLUSION_PHASE {index} {phase} ")
    verify(index, phase, records)
    click("nativeapi-occlusion-next", f"{index}-next")
wait("NATIVEAPI_OCCLUSION_FINAL_PASS")
records = wait("NATIVEAPI_OCCLUSION_UI_COMPLETE")
assert re.search(r"NATIVEAPI_OCCLUSION_FINAL_PASS passed=\d+ failed=0", records)
last_scene = next(
    line
    for line in records.splitlines()
    if "NATIVEAPI_OCCLUSION_PHASE 13 reshown " in line
)
last_properties = re.search(r" target=(\{.*\}) cover=(\{.*\})", last_scene)
assert last_properties
closed_ids = {json.loads(value)["id"] for value in last_properties.groups()}
deadline = time.monotonic() + 10
while closed_ids.intersection(windows("closed")) and time.monotonic() < deadline:
    time.sleep(0.1)
assert not closed_ids.intersection(windows("closed")), (
    "A destroyed child remained in the native window service"
)
print(f"NATIVEAPI_OCCLUSION_SUITE_PASS kind={kind} pid={app_pid} evidence={out}")
