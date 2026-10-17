#!/usr/bin/env python3
"""SDK/WMS confirmation of layout, SDK faults and native destruction."""

import argparse
import json
import re
from pathlib import Path

from ohos_ui_fixture import Fixture

p = argparse.ArgumentParser()
p.add_argument("deveco", type=Path)
p.add_argument("--device", default="127.0.0.1:5555")
a = p.parse_args()
f = Fixture(a.deveco, a.device, "window-layout", "LAYOUT")
raw = (f.out / "bundle-metadata.json").read_text()
app = json.loads(raw[raw.index("{") :])["applicationInfo"]
assert app["appPrivilegeLevel"] == "normal" and app["isSystemApp"] is False


def record(marker):
    logs = f.wait(marker)
    return json.loads(
        next(s.split(marker, 1)[1] for s in logs.splitlines() if marker in s)
    )


reference = record("NATIVEAPI_LAYOUT_REFERENCE ")
assert reference["available"] == (f.kind == "2in1")
assert [int(p["isLayoutFullScreen"]) for p in reference["enabled"]] == (
    [0, 1, 1] if f.kind == "2in1" else [1, 1, 1]
)
phases = ["baseline"]
for name in ["main", "child", "ordinary"]:
    phases += [
        name + "-" + action
        for action in [
            "cpp-ui-enable",
            "c-worker-disable",
            "c-ui-enable",
            "cpp-worker-disable",
            "cpp-worker-repeat-disable",
            "c-worker-enable",
            "cpp-ui-disable",
            "sdk-enable",
            "sdk-disable",
        ]
    ]
phases += [
    "fault-on",
    "fault-cpp-ui",
    "fault-c-worker",
    "nonpromise-on",
    "nonpromise-c-ui",
    "nonpromise-cpp-worker",
    "normal",
    "hold-ui",
    "overlap-c-ui",
    "overlap-cpp-worker",
    "release-ui",
    "retry-c-worker",
    "hold-worker",
    "close-children",
    "late-sdk-result",
    "closed-c-worker",
    "invalid",
    "finish",
]
scenes = []
main_content = None
for i, phase in enumerate(phases):
    before = record(f"NATIVEAPI_LAYOUT_BEFORE {i} {phase} ")
    state = record(f"NATIVEAPI_LAYOUT_PHASE {i} {phase} ")
    state["before"] = before
    after = state["snapshot"]
    n, old = after["native"], before["native"]
    full = n["native"]
    assert full["native"]["failed"] == 0
    pending = phase in ["hold-ui", "overlap-c-ui", "overlap-cpp-worker", "hold-worker"]
    assert n["pending"] == ([0, 1, 0] if pending else [0, 0, 0])
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
        sdk, target = after["windows"][j], full["native"]["windows"][j]
        events = full["events"][j]
        assert events[0] == events[1] == events[2]
        assert len(set(target["events"])) == 1
        if sdk is None:
            assert (
                not target["alive"] and not n["layout"][j] and not full["fullscreen"][j]
            )
            continue
        assert target["alive"] and n["layout"][j] == int(sdk["isLayoutFullScreen"])
        assert sdk["windowRect"] == reference["bases"][j]
        assert after["status"][j] == reference["status"][j]
        assert full["fullscreen"][j] == (
            after["status"][j] == 1 if reference["available"] else sdk["isFullScreen"]
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
        content, drawable = n["content"][j], sdk["drawableRect"]
        for key, expected in [
            ("x", rect["left"] + drawable["left"]),
            ("y", rect["top"] + drawable["top"]),
            ("width", drawable["width"]),
            ("height", drawable["height"]),
        ]:
            assert abs(content[key] * after["density"][j] - expected) < 0.03
        previous, current = old["native"]["fullscreen"][j], full["fullscreen"][j]
        counts = old["native"]["events"][j][0]
        assert events[0] == [
            counts[0] + int(not previous and current),
            counts[1] + int(previous and not current),
        ]
    accepted = state["accepted"]
    if any(phase.startswith(name + "-") for name in ["main", "child", "ordinary"]):
        desired = int(phase.endswith("-enable"))
        expected = (
            int(reference["enabled"][state["target"]]["isLayoutFullScreen"])
            if desired
            else 0
        )
        if "sdk-" not in phase:
            assert accepted == (int(expected == desired) if "worker" in phase else 1)
            if "worker" in phase:
                assert n["observed"] == expected
        assert n["layout"][state["target"]] == expected
        assert n["sdkCalls"][len(old["sdkCalls"]) :] == [
            [state["target"], int(phase.endswith("-enable"))]
        ]
    if phase in [
        "fault-cpp-ui",
        "fault-c-worker",
        "nonpromise-c-ui",
        "nonpromise-cpp-worker",
    ]:
        assert accepted == 0 and n["layout"] == old["layout"]
        assert n["sdkCalls"][len(old["sdkCalls"]) :] == [[1, 1]]
    if phase in ["hold-ui", "hold-worker"]:
        assert n["held"][1] == [1, 1, 1, 0] and n["layout"][1] == 1
        assert n["sdkCalls"][len(old["sdkCalls"]) :] == [[1, 1]]
        if phase == "hold-ui":
            assert accepted == 1
    if phase in ["overlap-c-ui", "overlap-cpp-worker", "closed-c-worker"]:
        assert accepted == 0 and n["sdkCalls"] == old["sdkCalls"]
    if phase == "close-children":
        assert accepted == 0 and n["observed"] == 0
        assert n["calls"] == old["calls"] + 1 and n["held"][1] == [1, 1, 1, 0]
    if phase == "late-sdk-result":
        assert n["held"][1][0] == 0
        for key in ["layout", "pending", "sdkCalls", "calls"]:
            assert n[key] == old[key]
        assert full["events"] == old["native"]["events"]
    if phase == "retry-c-worker":
        assert accepted == 1 and n["layout"][1] == 0 and n["observed"] == 0
    if phase in [
        "baseline",
        "main-cpp-ui-enable",
        "main-c-worker-disable",
        "child-cpp-ui-enable",
        "hold-worker",
        "finish",
    ]:
        tree = f.tree(f"{i}-{phase}")
        f.screenshot(f"{i}-{phase}")
        roots = [
            r
            for r in tree["children"]
            if r["attributes"].get("bundleName") == f.bundle
            and r["attributes"].get("hostWindowId") == str(after["windows"][0]["id"])
        ]
        assert len(roots) == 1
        columns = [
            node["attributes"]
            for node in f.walk(roots[0])
            if node["attributes"].get("type") == "Column"
        ]

        def rectangle(column):
            return list(map(int, re.findall(r"-?\d+", column["origBounds"])))

        assert columns
        content = max(
            (rectangle(c) for c in columns), key=lambda r: (r[2] - r[0]) * (r[3] - r[1])
        )
        state["mainContent"] = content
        state["mainVisibleContent"] = [c["bounds"] for c in columns]
        if phase == "baseline":
            main_content = content
        elif phase == "main-cpp-ui-enable":
            assert main_content is not None
            if reference["available"]:
                assert content == main_content
            else:
                assert (
                    content[1] < main_content[1]
                    and content[3] - content[1]
                    > main_content[3] - main_content[1] + 100
                )
        elif phase == "main-c-worker-disable":
            assert content == main_content
    (f.out / f"{i}-{phase}.json").write_text(json.dumps(state, indent=2))
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
        "window-layout",
        "--ps",
        "nativeapiLayoutAck",
        str(i + 1),
    )
records = f.wait("NATIVEAPI_LAYOUT_DONE")
assert "NATIVEAPI_LAYOUT_FINAL_PASS" in records
final = scenes[-1]["snapshot"]["native"]
assert len(scenes) == 46 and final["calls"] == 31 and len(final["sdkCalls"]) == 34
assert final["native"]["native"]["passed"] == 947
summary = dict(
    guest=f.instance["name"],
    pid=f.pid,
    uid=f.uid,
    native=final,
    scenes=len(scenes),
    evidence=str(f.out),
)
(f.out / "summary.json").write_text(json.dumps(summary, indent=2))
print("PASS", json.dumps(summary), flush=True)
