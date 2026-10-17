#!/usr/bin/env python3
"""Prove SDK aspect constraints with guarded native guest mouse edge drags."""

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
f = Fixture(a.deveco, a.device, "window-aspect", "ASPECT")
records = f.wait("NATIVEAPI_ASPECT_READY")
ready = re.search(
    r"NATIVEAPI_ASPECT_READY available=(true|false) desktop=(true|false) main=(\{.*\}) child=(\{.*\})",
    records,
)
assert ready, records[-5000:]
available, desktop = [s == "true" for s in ready.groups()[:2]]
assert desktop == (f.kind == "2in1")
main, child = map(json.loads, ready.groups()[2:])
phases = [
    "baseline",
    "square",
    "programmatic",
    "wide",
    "invalid",
    "reject",
    "queued",
    "sdk",
    "restore-library",
    "hide-decor",
    "show-decor",
    "clear",
    "negative",
    "child",
    "limited",
    "restore",
]
ratios = [0, 1, 1, 1.5, 1.5, 1.5, 1.25, 2, 1.25, 1.25, 1.25, 0, 0, 0, 1.5, 0]


def frame(phase):
    f.windows(phase)
    raw = (f.out / f"{phase}-windows.txt").read_text()
    pattern = (
        r"^(.+?)\s+(\d+)\s+(\d+)\s+(\d+)\s+(\d+)\s+(\d+)\s+(\d+)\s+(-?\d+)\s+"
        r"(\d+)\s+\[\s*(-?\d+)\s+(-?\d+)\s+(\d+)\s+(\d+)\s*\]"
    )
    rows = [
        m
        for line in raw.splitlines()
        if (m := re.match(pattern, line)) and m[3] == f.pid and int(m[4]) == main["id"]
    ]
    assert len(rows) == 1 and int(rows[0][8]) > 0
    return tuple(map(int, rows[0].groups()[9:13]))


def drag(phase, target, ratio):
    before = frame(phase + "-before-drag")
    # Start inside the straight right border, away from transparent rounded corners.
    x, y = before[0] + before[2] - 2, before[1] + before[3] // 2
    tree = f.tree(phase + "-drag-owner")
    hits = [
        r
        for r in tree["children"]
        if f.rect(r)[0] <= x < f.rect(r)[2] and f.rect(r)[1] <= y < f.rect(r)[3]
    ]
    assert (
        len(hits) == 1
        and hits[0]["attributes"].get("bundleName") == "dev.nativeapi.smoke"
    )
    assert int(hits[0]["attributes"]["hostWindowId"]) == main["id"]
    f.identity()
    (f.out / f"{phase}-drag-owner.json").write_text(
        json.dumps(
            {
                "pid": f.pid,
                "uid": f.uid,
                "window": main["id"],
                "point": [x, y],
                "root": hits[0]["attributes"],
            }
        )
    )
    delta = 400 if phase == "limited" else 80
    answer = f.guest("uinput", "-M", "-g", x, y, x + delta, y, 600)
    assert not re.search(r"failed|invalid|error", answer, re.IGNORECASE), answer
    deadline = time.monotonic() + 8
    while time.monotonic() < deadline:
        after = frame(phase + "-after-drag")
        if abs(after[2] - before[2]) > 20 or abs(after[3] - before[3]) > 20:
            break
        time.sleep(0.15)
    else:
        raise AssertionError(("No real native edge resize", phase, before, after))
    time.sleep(0.3)
    after = frame(phase + "-released")
    # SDK drawableRect is measured independently before resizing; decor stays fixed.
    content = target["drawableRect"]
    dw, dh = before[2] - content["width"], before[3] - content["height"]
    actual = (after[2] - dw) / (after[3] - dh)
    if ratio:
        assert abs(actual - ratio) < 0.01, (
            phase,
            before,
            after,
            [dw, dh],
            actual,
            ratio,
        )
        if phase == "limited":
            match = re.search(r"NATIVEAPI_ASPECT_LIMITS (\{.*\})", f.logs())
            assert match
            limits = json.loads(match[1])
            assert abs(after[2] - limits["maxWidth"]) <= 3, (after, limits)
            assert limits["minHeight"] <= after[3] <= limits["maxHeight"]
    else:
        assert abs(after[2] - before[2] - 80) <= 10
        assert abs(after[3] - before[3]) <= 10
    assert abs(after[0] - before[0]) <= 2
    (f.out / f"{phase}-resize-result.json").write_text(
        json.dumps(
            {
                "before": before,
                "after": after,
                "decor": [dw, dh],
                "actual_ratio": actual,
                "expected_ratio": ratio,
            }
        )
    )


f.click("nativeapi-aspect-run", "start")
dragged = 0
for i, phase in enumerate(phases):
    marker = f"NATIVEAPI_ASPECT_PHASE {i} {phase} "
    records = f.wait(marker)
    line = next(s for s in records.splitlines() if marker in s)
    match = re.search(r"native=(\{.*\}) before=(\{.*\}) target=(\{.*\})", line)
    assert match, line
    state, before, target = map(json.loads, match.groups())
    assert state["failed"] == 0 and state["pending"] == 0
    assert target["id"] == main["id"]
    rect = target["windowRect"]
    assert frame(phase) == tuple(rect[k] for k in ["left", "top", "width", "height"])
    if phase not in ["programmatic", "hide-decor", "show-decor"] or not desktop:
        assert rect == before["windowRect"], (
            "setter unexpectedly resized window",
            phase,
            before,
            target,
        )
    if phase == "programmatic" and desktop:
        c = target["drawableRect"]
        assert abs(c["width"] / c["height"] - 1) > 0.2
    elif desktop:
        drag(phase, target, ratios[i] if available else 0)
        dragged += 1
    f.screenshot(phase)
    print("PASS real aspect scene", i, phase, state["ratio"], flush=True)
    f.click("nativeapi-aspect-next", phase + "-next")
records = f.wait("NATIVEAPI_ASPECT_UI_COMPLETE")
assert "NATIVEAPI_ASPECT_FINAL_PASS" in records
assert child["id"] not in f.windows("closed")
(f.out / "result.json").write_text(
    json.dumps(
        {
            "pid": f.pid,
            "kind": f.kind,
            "available": available,
            "scenes": len(phases),
            "mouse_resizes": dragged,
        }
    )
)
print(
    f"PASS {len(phases)} aspect scenes/{dragged} native resizes; PID {f.pid}; evidence {f.out}",
    flush=True,
)
