#!/usr/bin/env python3
"""Prove native dragging with real, guarded guest touch gestures and WMS geometry."""

import argparse
import json
import re
import time
from pathlib import Path

from ohos_ui_fixture import Fixture

p = argparse.ArgumentParser()
p.add_argument("deveco", type=Path)
p.add_argument("--device", default="127.0.0.1:5555")
a = p.parse_args()
f = Fixture(a.deveco, a.device, "window-drag", "DRAG")
raw = (f.out / "bundle-metadata.json").read_text()
metadata = json.loads(raw[raw.index("{") :])
assert metadata["applicationInfo"]["appPrivilegeLevel"] == "normal"
assert metadata["applicationInfo"]["isSystemApp"] is False
assert "ohos.permission.SET_WINDOW_TOUCH_AREAS" not in metadata["reqPermissions"]
records = f.wait("NATIVEAPI_DRAG_READY")
for label in ["main", "child"]:
    match = re.search(
        r"NATIVEAPI_DRAG_INPUT_PERMISSION " + label + r" code=(\d+)", records
    )
    assert match and int(match[1]) in [201, 801], records[-3000:]
f.click("nativeapi-drag-run", "start")
phases = [
    "child-cpp",
    "child-c",
    "child-sdk",
    "child-off",
    "main-cpp",
    "main-c",
    "main-sdk",
    "main-off",
]


def geometry(window_id, phase):
    f.windows(phase)
    records = (f.out / f"{phase}-windows.txt").read_text()
    pattern = (
        r"^(.+?)\s+(\d+)\s+(\d+)\s+(\d+)\s+(\d+)\s+(\d+)\s+(\d+)\s+(-?\d+)\s+"
        r"(\d+)\s+\[\s*(-?\d+)\s+(-?\d+)\s+(\d+)\s+(\d+)\s*\]"
    )
    matches = [
        m
        for line in records.splitlines()
        if (m := re.match(pattern, line)) and m[3] == f.pid and int(m[4]) == window_id
    ]
    assert len(matches) == 1, records[-5000:]
    assert int(matches[0][8]) > 0
    return tuple(map(int, matches[0].groups()[9:13]))


def pad(main, phase):
    t = f.tree(phase)
    key = "nativeapi-main-drag-pad" if main else "nativeapi-child-drag-pad"
    nodes = [
        (root, node)
        for root in t["children"]
        if root["attributes"].get("bundleName") == "dev.nativeapi.smoke"
        for node in f.walk(root)
        if node["attributes"].get("id") == key
    ]
    assert len(nodes) == 1, nodes
    root, node = nodes[0]
    left, top, right, bottom = f.rect(node)
    assert right > left and bottom > top
    x, y = (left + right) // 2, (top + bottom) // 2
    hits = [
        r
        for r in f.tree(phase + "-fresh")["children"]
        if f.rect(r)[0] <= x < f.rect(r)[2] and f.rect(r)[1] <= y < f.rect(r)[3]
    ]
    assert hits and all(
        r["attributes"].get("bundleName") == "dev.nativeapi.smoke" for r in hits
    )
    rows = f.windows(phase + "-owner")
    wid = int(root["attributes"]["hostWindowId"])
    assert all(int(r["attributes"]["hostWindowId"]) in rows for r in hits)
    assert int(rows[wid][7]) == max(
        int(rows[int(r["attributes"]["hostWindowId"])][7]) for r in hits
    )
    f.identity()
    (f.out / f"{phase}-owner.json").write_text(
        json.dumps(
            dict(
                pid=f.pid,
                uid=f.uid,
                window=wid,
                point=[x, y],
                node=node["attributes"],
                roots=[r["attributes"] for r in hits],
            )
        )
    )
    return wid, x, y


previous = None
for index, phase in enumerate(phases):
    marker = f"NATIVEAPI_DRAG_PHASE {index} {phase} "
    records = f.wait(marker)
    line = next(line for line in records.splitlines() if marker in line)
    match = re.search(r"native=(\{.*\}) target=(\{.*\})", line)
    assert match, line
    state, target = map(json.loads, match.groups())
    assert state["failed"] == 0
    if previous and previous[2]:
        assert state[previous[0]] > previous[1], ("No real move event", previous)
    main = index >= 4
    wid, x, y = pad(main, phase)
    assert wid == target["id"]
    before = geometry(wid, phase + "-before")
    assert before == tuple(
        target["windowRect"][k] for k in ["left", "top", "width", "height"]
    )
    should_move = not phase.endswith("off") and (not main or f.kind == "2in1")
    expected = (
        (before[0] + 80, before[1] + 40, before[2], before[3])
        if should_move
        else before
    )
    result = f.run(
        f.emulator,
        "-instance",
        f.instance["name"],
        "-slide",
        f"{x} {y} {x + 80} {y + 40}",
    )
    assert "Scenario simulation success" in result, result
    deadline = time.monotonic() + 8
    while True:
        current = geometry(wid, phase + "-after")
        if all(abs(current[i] - expected[i]) <= 8 for i in range(4)):
            break
        if time.monotonic() > deadline:
            raise AssertionError((phase, before, current, expected))
        time.sleep(0.15)
    time.sleep(0.25)
    assert all(
        abs(geometry(wid, phase + "-released")[i] - current[i]) <= 1 for i in range(4)
    )
    records = f.logs()
    assert (
        f"NATIVEAPI_DRAG_DOWN {'main' if main else 'child'} {phase.split('-')[1]}"
        in records
    )
    if phase.endswith("sdk"):
        result = re.findall(
            r"NATIVEAPI_DRAG_REFERENCE "
            + ("main" if main else "child")
            + r" code=(\d+)",
            records,
        )
        assert result
        assert (
            int(result[-1]) == 0 if should_move else int(result[-1]) in [801, 1300004]
        )
    touch_after = current
    if f.kind == "2in1":
        _, mx, my = pad(main, phase + "-mouse")
        mouse_before = current
        mouse_expected = (
            (current[0] - 40, current[1] - 20, current[2], current[3])
            if should_move
            else current
        )
        result = f.guest("uinput", "-M", "-g", mx, my, mx - 40, my - 20, 500)
        assert not re.search(r"failed|invalid|error", result, re.I), result
        deadline = time.monotonic() + 8
        while True:
            current = geometry(wid, phase + "-mouse-after")
            if all(abs(current[i] - mouse_expected[i]) <= 8 for i in range(4)):
                break
            if time.monotonic() > deadline:
                raise AssertionError(
                    ("mouse", phase, mouse_before, current, mouse_expected)
                )
            time.sleep(0.15)
        (f.out / f"{phase}-mouse-result.json").write_text(
            json.dumps(
                dict(before=mouse_before, after=current, expected=mouse_expected)
            )
        )
    f.screenshot(phase)
    # A fresh click from rest must not move the window or leave a drag latched.
    _, cx, cy = pad(main, phase + "-click")
    result = f.run(f.emulator, "-instance", f.instance["name"], "-click", f"{cx} {cy}")
    assert "Scenario simulation success" in result, result
    time.sleep(0.2)
    assert all(
        abs(geometry(wid, phase + "-click-after")[i] - current[i]) <= 1
        for i in range(4)
    )
    (f.out / f"{phase}-result.json").write_text(
        json.dumps(
            dict(
                before=before,
                after=current,
                expected=mouse_expected if f.kind == "2in1" else expected,
                touch_after=touch_after,
                touch_expected=expected,
                should_move=should_move,
            )
        )
    )
    print("PASS real native drag, release and click-only geometry", phase, flush=True)
    previous = (
        "mainMoves" if main else "childMoves",
        state["mainMoves" if main else "childMoves"],
        should_move,
        phase,
    )
    f.click("nativeapi-drag-next", phase + "-next")
records = f.wait("NATIVEAPI_DRAG_UI_COMPLETE")
assert "NATIVEAPI_DRAG_FINAL_PASS" in records
(f.out / "result.json").write_text(
    json.dumps(dict(pid=f.pid, kind=f.kind, scenes=len(phases)))
)
print(
    f"PASS {len(phases)} native drag scenes; PID {f.pid}; evidence {f.out}", flush=True
)
