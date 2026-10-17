#!/usr/bin/env python3
"""Real compositor mask pixels and owner-guarded touch routing in disposable guests."""

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
f = Fixture(a.deveco, a.device, "window-shape", "SHAPE")
records = f.wait("NATIVEAPI_SHAPE_READY")
ready = re.search(r"NATIVEAPI_SHAPE_READY density=([\d.]+) target=(\{.*\})", records)
assert ready, records[-4000:]
assert float(ready[1]) > 0
f.click("nativeapi-shape-run", "start")
phases = [
    "empty-clear",
    "pending",
    "clear",
    "concave",
    "clear",
    "bow",
    "outside",
    "rectangle",
    "clear",
    "resize-triangle",
    "clear",
    "reference",
    "clear",
]
target_id = None
initial = None
samples_total = 0
probes_total = 0


def inside(phase, x, y):
    if phase == "concave":
        return 0.1 < x < 0.9 and 0.1 < y < 0.9 and (x < 0.4 or y < 0.4)
    if phase == "bow":
        return (
            0.1 < x < 0.9
            and 0.1 < y < 0.9
            and (y < 0.5 and y < x < 1 - y or y > 0.5 and 1 - y < x < y)
        )
    if phase in ["pending", "resize-triangle"]:
        return x > 0.1 and y > 0.1 and x + y < 1
    if phase == "outside":
        return False
    if phase == "reference":
        return x < 0.5
    return True


def guarded_touch(x, y, expected, phase, target_id):
    t = f.tree(phase)
    hits = [
        r
        for r in t["children"]
        if f.rect(r)[0] <= x < f.rect(r)[2] and f.rect(r)[1] <= y < f.rect(r)[3]
    ]
    assert hits and all(
        r["attributes"].get("bundleName") == "dev.nativeapi.smoke" for r in hits
    ), hits
    rows = f.windows(phase)
    assert target_id in rows and int(rows[target_id][7]) > 0
    for root in hits:
        row = rows[int(root["attributes"]["hostWindowId"])]
        assert row[2] == f.pid and int(row[7]) > 0
    f.identity()
    before = f.logs().count("NATIVEAPI_SHAPE_TOUCH ")
    (f.out / f"{phase}-touch-owner.json").write_text(
        json.dumps(
            dict(
                pid=f.pid,
                uid=f.uid,
                point=[x, y],
                roots=[r["attributes"] for r in hits],
                expected=expected,
            )
        )
    )
    result = f.run(f.emulator, "-instance", f.instance["name"], "-click", f"{x} {y}")
    assert "Scenario simulation success" in result, result
    deadline = time.monotonic() + 8
    while time.monotonic() < deadline:
        touches = re.findall(r"NATIVEAPI_SHAPE_TOUCH (main|target)", f.logs())
        if len(touches) > before:
            assert len(touches) == before + 1 and touches[-1] == expected, (
                phase,
                expected,
                touches[-4:],
            )
            return
        time.sleep(0.1)
    raise AssertionError(("Missing real touch callback", phase, expected))


for index, phase in enumerate(phases):
    marker = f"NATIVEAPI_SHAPE_PHASE {index} {phase} "
    records = f.wait(marker)
    line = next(line for line in records.splitlines() if marker in line)
    values = re.search(r"native=(\{.*\}) target=(\{.*\})", line)
    assert values, line
    state, target = map(json.loads, values.groups())
    assert state["failed"] == 0
    frame = target["windowRect"]
    if target_id is None:
        target_id = target["id"]
        initial = frame
    assert target["id"] == target_id
    assert frame["left"] == initial["left"] and frame["top"] == initial["top"]
    if index < 9:
        assert frame == initial
    else:
        assert abs(frame["width"] - 200 * float(ready[1])) <= 1
        assert abs(frame["height"] - 140 * float(ready[1])) <= 1
    row = f.windows(f"{index}-{phase}")[target_id]
    assert row[2] == f.pid and int(row[7]) > 0
    picture = f.screenshot(f"{index}-{phase}")
    samples = []
    for fx in [0.03, 0.16, 0.27, 0.37, 0.57, 0.68, 0.79, 0.97]:
        for fy in [0.03, 0.16, 0.27, 0.37, 0.57, 0.68, 0.79, 0.97]:
            x = frame["left"] + round(fx * frame["width"])
            y = frame["top"] + round(fy * frame["height"])
            # Independent geometric oracle; avoid raster boundaries.
            px = (x - frame["left"] + 0.5) / frame["width"]
            py = (y - frame["top"] + 0.5) / frame["height"]
            if abs(px - py) < 0.02 or abs(px + py - 1) < 0.02:
                continue
            expected = (255, 0, 255) if inside(phase, px, py) else (255, 255, 255)
            actual = picture.getpixel((x, y))
            assert all(abs(actual[i] - expected[i]) <= 8 for i in range(3)), (
                index,
                phase,
                [x, y],
                actual,
                expected,
            )
            samples.append(dict(point=[x, y], actual=actual, expected=expected))
    assert len(samples) >= 40
    samples_total += len(samples)
    (f.out / f"{index}-{phase}-pixels.json").write_text(json.dumps(samples))
    for point_index, (fx, fy) in enumerate([(0.37, 0.27), (0.79, 0.68)]):
        x = frame["left"] + round(fx * frame["width"])
        y = frame["top"] + round(fy * frame["height"])
        px = (x - frame["left"] + 0.5) / frame["width"]
        py = (y - frame["top"] + 0.5) / frame["height"]
        expected = "target" if inside(phase, px, py) else "main"
        guarded_touch(x, y, expected, f"{index}-{phase}-probe-{point_index}", target_id)
        probes_total += 1
    print(
        "PASS native window mask scene and real touch routing", index, phase, flush=True
    )
    f.click("nativeapi-shape-next", f"{index}-{phase}-next")
records = f.wait("NATIVEAPI_SHAPE_UI_COMPLETE")
assert "NATIVEAPI_SHAPE_FINAL_PASS" in records
assert target_id not in f.windows("closed")
(f.out / "result.json").write_text(
    json.dumps(
        dict(
            pid=f.pid,
            kind=f.kind,
            scenes=len(phases),
            pixel_samples=samples_total,
            touch_probes=probes_total,
        )
    )
)
print(
    f"PASS {len(phases)} native mask scenes, {samples_total} pixels, "
    f"{probes_total} guarded touches; PID {f.pid}; evidence {f.out}",
    flush=True,
)
