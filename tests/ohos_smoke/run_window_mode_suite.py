#!/usr/bin/env python3
"""Public mode-mask enforcement, real SDK delegation and actual WMS geometry."""

import argparse
import json
import re
from pathlib import Path

from ohos_ui_fixture import Fixture

p = argparse.ArgumentParser()
p.add_argument("deveco", type=Path)
p.add_argument("--device", default="127.0.0.1:5555")
a = p.parse_args()
f = Fixture(a.deveco, a.device, "window-mode-policy", "MODE")
raw = (f.out / "bundle-metadata.json").read_text()
app = json.loads(raw[raw.index("{") :])["applicationInfo"]
assert app["appPrivilegeLevel"] == "normal" and app["isSystemApp"] is False


def record(marker):
    logs = f.wait(marker)
    return json.loads(
        next(line.split(marker, 1)[1] for line in logs.splitlines() if marker in line)
    )


reference = record("NATIVEAPI_MODE_REFERENCE ")
available = f.kind == "2in1"
assert reference["available"] == available and reference["declared"] == [0, 1, 2]
phases = [
    "baseline",
    "main-cpp-ui-disable",
    "main-disabled-sdk-full",
    "main-disabled-native-full",
    "main-native-exit",
    "main-c-worker-enable",
    "main-native-enter",
    "main-c-native-exit",
    "main-ui-batch",
    "main-worker-enable",
    "main-repeat-enable",
    "fallback-on",
    "fallback-disable",
    "fallback-enable",
    "fallback-off",
    "host-main-floating-only",
    "main-enable-no-split",
    "main-disable-no-split",
    "host-main-restore",
    "child-cpp-ui-enable",
    "child-native-enter",
    "child-native-exit",
    "child-c-worker-disable",
    "child-disabled-sdk-full",
    "child-worker-enable",
    "child-ui-batch",
    "child-worker-restore",
    "ordinary-cpp-enable",
    "ordinary-native-enter",
    "ordinary-native-exit",
    "ordinary-c-disable",
    "ordinary-disabled-sdk-full",
    "host-ordinary-full-only",
    "ordinary-reject-empty",
    "host-ordinary-restore",
    "invalid-baselines",
    "pending-prepare",
    "close-children",
    "late-sdk-completion",
    "closed-c-worker",
    "closed-cpp-ui",
    "invalid",
    "finish",
]
scenes = []
for i, phase in enumerate(phases):
    before = record(f"NATIVEAPI_MODE_BEFORE {i} {phase} ")
    state = record(f"NATIVEAPI_MODE_PHASE {i} {phase} ")
    after = state["snapshot"]
    n = after["native"]
    assert n["native"]["failed"] == 0
    assert n["pending"] == ([0, 4, 0] if phase == "pending-prepare" else [0, 0, 0])
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
        target, sdk = n["native"]["windows"][j], after["windows"][j]
        assert len(set(n["events"][j])) == len(set(target["events"])) == 1
        if sdk is None:
            assert not target["alive"] and not n["policy"][j] and not n["fullscreen"][j]
            assert target["bounds"] == dict(x=0, y=0, width=0, height=0)
            continue
        assert target["alive"]
        assert n["fullscreen"][j] == (
            after["status"][j] == 1 if available else sdk["isFullScreen"]
        )
        rect = sdk["windowRect"]
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
        if not available and j > 0:
            assert rect == reference["bases"][j]
        delta = abs(int(n["policy"][j]) - int(before["native"]["policy"][j]))
        if (
            phase in ["main-ui-batch", "child-ui-batch"]
            and j == (0 if phase.startswith("main") else 1)
            and available
        ):
            delta = 3
        assert n["events"][j][0] == before["native"]["events"][j][0] + delta
    if available:
        if phase == "main-cpp-ui-disable":
            assert n["sdkCalls"][-1] == [0, 6] and not n["policy"][0]
        if phase == "main-disabled-sdk-full":
            assert state["code"] == 1300002 and after["status"][0] == 4
        if phase == "main-disabled-native-full":
            assert after["status"][0] == 4 and not n["fullscreen"][0]
        if phase == "fallback-disable":
            assert n["sdkCalls"][-1] == [3, 6] and not n["policy"][0]
        if phase == "fallback-enable":
            assert n["sdkCalls"][-1] == [3, 7] and n["policy"][0]
        if phase == "main-enable-no-split":
            assert n["sdkCalls"][-1] == [0, 5] and n["masks"][0] == 5
        if phase == "main-disable-no-split":
            assert n["sdkCalls"][-1] == [0, 4] and n["masks"][0] == 4
        if phase in ["child-disabled-sdk-full", "ordinary-disabled-sdk-full"]:
            assert state["code"] == 1300004
        if phase in [
            "child-native-enter",
            "ordinary-native-enter",
            "host-ordinary-full-only",
            "main-native-enter",
        ]:
            j = (
                1
                if phase.startswith("child")
                else (0 if phase.startswith("main") else 2)
            )
            assert after["status"][j] == 1 and n["fullscreen"][j]
            rect = after["windows"][j]["windowRect"]
            assert (
                rect["left"] == rect["top"] == 0
                and rect["width"] > reference["bases"][j]["width"] + 100
            )
        if phase == "ordinary-reject-empty":
            assert (
                n["sdkCalls"] == before["native"]["sdkCalls"]
                and n["policy"][2]
                and after["status"][2] == 1
            )
    else:
        assert n["policy"] == [True, False, False]
        assert n["events"] == [[0, 0, 0]] * 3
    if phase in ["main-ui-batch", "child-ui-batch"]:
        target = 0 if phase.startswith("main") else 1
        assert n["sdkCalls"][-3:] == [
            [target, 6 if target == 0 else 4],
            [target, 7 if target == 0 else 5],
            [target, 6 if target == 0 else 4],
        ]
    if phase == "pending-prepare":
        assert n["held"][1] == ([1, 1, 1, 0] if available else [1, 1, 0, 801])
        assert n["sdkCalls"][len(before["native"]["sdkCalls"]) :] == [[1, 4]]
        assert (
            after["status"][1] == 4
            and after["windows"][1]["windowRect"] == reference["bases"][1]
        )
        if available:
            assert state["code"] == 1300004
    if phase == "close-children":
        assert n["sdkCalls"] == before["native"]["sdkCalls"] and n["observed"] is False
    if phase == "late-sdk-completion":
        assert n["held"][1][0] == 0
        assert (
            n["sdkCalls"] == before["native"]["sdkCalls"]
            and n["events"] == before["native"]["events"]
        )
    if phase == "invalid-baselines":
        assert n["sdkCalls"] == before["native"]["sdkCalls"]
    if phase in [
        "main-c-native-exit",
        "child-native-exit",
        "ordinary-native-exit",
        "host-ordinary-restore",
    ]:
        for j, sdk in enumerate(after["windows"]):
            assert sdk["windowRect"] == reference["bases"][j]
    state["before"] = before
    (f.out / f"{i}-{phase}.json").write_text(json.dumps(state, indent=2))
    if phase in [
        "main-cpp-ui-disable",
        "main-enable-no-split",
        "ordinary-native-enter",
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
        "window-mode-policy",
        "--ps",
        "nativeapiModeAck",
        str(i + 1),
    )
records = f.wait("NATIVEAPI_MODE_DONE")
assert "NATIVEAPI_MODE_FINAL_PASS" in records
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
