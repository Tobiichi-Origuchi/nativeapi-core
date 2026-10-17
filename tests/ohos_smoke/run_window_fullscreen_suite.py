#!/usr/bin/env python3
"""SDK/WMS confirmation of main and child native fullscreen transitions."""

import argparse
import json
import re
from pathlib import Path

from ohos_ui_fixture import Fixture

p = argparse.ArgumentParser()
p.add_argument("deveco", type=Path)
p.add_argument("--device", default="127.0.0.1:5555")
a = p.parse_args()
f = Fixture(a.deveco, a.device, "window-fullscreen", "FULLSCREEN")
raw = (f.out / "bundle-metadata.json").read_text()
app = json.loads(raw[raw.index("{") :])["applicationInfo"]
assert app["appPrivilegeLevel"] == "normal" and app["isSystemApp"] is False


def record(marker):
    logs = f.wait(marker)
    return json.loads(
        next(line.split(marker, 1)[1] for line in logs.splitlines() if marker in line)
    )


reference = record("NATIVEAPI_FULLSCREEN_REFERENCE ")
available = f.kind == "2in1"
assert reference["available"] == available
phases = [
    "baseline",
    "main-cpp-ui-enter",
    "main-c-ui-repeat-enter",
    "main-c-ui-exit",
    "main-cpp-worker-repeat-exit",
    "main-c-worker-enter",
    "main-cpp-worker-exit",
    "child-cpp-ui-enter",
    "child-c-worker-repeat-enter",
    "child-c-ui-exit",
    "child-cpp-ui-repeat-exit",
    "child-c-worker-enter",
    "child-cpp-worker-exit",
    "child-maximized",
    "child-cpp-worker-enter-maximized",
    "child-c-ui-exit-maximized",
    "child-recover",
    "ordinary-cpp-enter",
    "ordinary-c-worker-enter",
    "sdk-child-enter",
    "sdk-child-exit",
    "child-restrict-modes",
    "child-restricted-enter",
    "child-restore-modes",
    "child-restored-enter",
    "child-restored-exit",
    "close-children",
    "closed-cpp-ui",
    "closed-c-worker",
    "invalid",
    "finish",
]
scenes = []
for i, phase in enumerate(phases):
    state = record(f"NATIVEAPI_FULLSCREEN_PHASE {i} {phase} ")
    before, after = state["before"], state["snapshot"]
    native = after["native"]
    assert native["native"]["failed"] == 0
    f.windows(f"{i}-{phase}")
    text = (f.out / f"{i}-{phase}-windows.txt").read_text()
    pattern = (
        r"^(.+?)\s+(\d+)\s+(\d+)\s+(\d+)\s+(\d+)\s+(\d+)\s+(\d+)\s+(-?\d+)\s+"
        r"(\d+)\s+\[\s*(-?\d+)\s+(-?\d+)\s+(\d+)\s+(\d+)\s*\]"
    )
    frames = {
        int(m[4]): tuple(map(int, m.groups()[9:13]))
        for line in text.splitlines()
        if (m := re.match(pattern, line)) and m[3] == f.pid
    }
    for j in range(3):
        sdk, target = after["windows"][j], native["native"]["windows"][j]
        counts = native["events"][j]
        assert counts[0] == counts[1] == counts[2]
        assert len(set(target["events"])) == 1
        if sdk is None:
            assert not target["alive"] and not native["fullscreen"][j]
            assert target["bounds"] == dict(x=0, y=0, width=0, height=0)
            continue
        assert target["alive"] and after["freeform"][j] == available
        expected_full = after["status"][j] == 1 if available else sdk["isFullScreen"]
        assert native["fullscreen"][j] == expected_full
        rect = sdk["windowRect"]
        if not available and j > 0:
            assert rect == reference["bases"][j]
        assert frames[sdk["id"]] == tuple(
            rect[k] for k in ["left", "top", "width", "height"]
        )
        for key, other in [
            ("x", "left"),
            ("y", "top"),
            ("width", "width"),
            ("height", "height"),
        ]:
            assert abs(target["bounds"][key] * after["density"][j] - rect[other]) < 0.03
        previous = before["native"]["fullscreen"][j]
        current = native["fullscreen"][j]
        old_counts = before["native"]["events"][j][0]
        assert counts[0] == [
            old_counts[0] + int(not previous and current),
            old_counts[1] + int(previous and not current),
        ]
        if available and current and not previous:
            assert rect["left"] == rect["top"] == 0
            assert rect["width"] > reference["bases"][j]["width"] + 100
            assert rect["height"] > reference["bases"][j]["height"] + 100
        if phase in [
            "main-c-ui-exit",
            "main-cpp-worker-exit",
            "child-c-ui-exit",
            "child-cpp-worker-exit",
            "child-recover",
            "sdk-child-exit",
            "child-restored-exit",
        ]:
            assert rect == reference["bases"][j]
    if phase == "child-maximized" and available:
        assert after["status"][1] == 2
        maximized = after["windows"][1]["windowRect"]
    if phase == "child-c-ui-exit-maximized" and available:
        assert after["status"][1] == 2
        assert after["windows"][1]["windowRect"] == maximized
    (f.out / f"{i}-{phase}.json").write_text(json.dumps(state, indent=2))
    if phase in [
        "main-cpp-ui-enter",
        "main-c-ui-repeat-enter",
        "child-cpp-ui-enter",
        "child-c-ui-exit-maximized",
        "finish",
    ]:
        f.tree(f"{i}-{phase}")
        f.screenshot(f"{i}-{phase}")
    scenes.append(state)
    f.identity()
    assert "start ability successfully" in f.guest(
        "aa",
        "start",
        "-a",
        "EntryAbility",
        "-b",
        f.bundle,
        "--ps",
        "nativeapiSuite",
        "window-fullscreen",
        "--ps",
        "nativeapiFullscreenAck",
        str(i + 1),
    )
records = f.wait("NATIVEAPI_FULLSCREEN_DONE")
assert "NATIVEAPI_FULLSCREEN_FINAL_PASS" in records
summary = dict(
    guest=f.instance["name"],
    pid=f.pid,
    uid=f.uid,
    native=scenes[-1]["snapshot"]["native"],
    scenes=len(scenes),
    evidence=str(f.out),
)
(f.out / "summary.json").write_text(json.dumps(summary, indent=2))
print("PASS", json.dumps(summary), flush=True)
