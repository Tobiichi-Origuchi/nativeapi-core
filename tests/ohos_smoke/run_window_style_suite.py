#!/usr/bin/env python3
"""Verify real native corner masks and shadow pixels against independent SDK calls."""

import argparse
import json
import math
import re
import time
from pathlib import Path

from ohos_ui_fixture import Fixture
from PIL import ImageChops

p = argparse.ArgumentParser()
p.add_argument("deveco", type=Path)
p.add_argument("--device", default="127.0.0.1:5555")
a = p.parse_args()
f = Fixture(a.deveco, a.device, "window-style", "STYLE")
records = f.wait("NATIVEAPI_STYLE_READY")
ready = re.search(
    r"NATIVEAPI_STYLE_READY initial=([\d.]+) density=([\d.]+) target=(\{.*\})", records
)
assert ready, records[-4000:]
density = float(ready[2])
assert density > 0
f.click("nativeapi-style-run", "start")
phases = [
    "pending",
    "small",
    "round",
    "default",
    "external-corner",
    "round",
    "square",
    "shadow-off",
    "shadow-on",
    "reference-off",
    "reference-on",
    "shadow-off",
    "shadow-on",
    "default",
]
base_rect = None
captures = {}
target_id = None
for index, phase in enumerate(phases):
    records = f.wait(f"NATIVEAPI_STYLE_PHASE {index} {phase} ")
    line = next(
        line
        for line in records.splitlines()
        if f"NATIVEAPI_STYLE_PHASE {index} {phase} " in line
    )
    values = re.search(r"radius=([\d.]+) native=(\{.*\}) target=(\{.*\})", line)
    assert values, line
    radius = float(values[1])
    state, target = map(json.loads, values.groups()[1:])
    assert state["failed"] == 0
    original = re.search(r"NATIVEAPI_STYLE_ORIGINAL ([\d.]+)", records)
    assert original
    expected = (
        12
        if phase == "external-corner"
        else 0
        if index == 0 or 6 <= index < 13
        else 4
        if phase == "small"
        else 8
        if phase == "round"
        else float(original[1])
    )
    assert abs(expected - radius) < 0.001, (index, phase, radius, expected)
    frame = target["windowRect"]
    if base_rect is None:
        base_rect = frame
        target_id = target["id"]
    assert frame == base_rect and target["id"] == target_id, (
        "Style mutation changed native geometry or identity"
    )
    row = f.windows(f"{index}-{phase}")[target_id]
    assert row[2] == f.pid and int(row[7]) > 0
    f.tree(f"{index}-{phase}-scene")
    picture = f.screenshot(f"{index}-{phase}")
    left, top, width, height = (frame[k] for k in ["left", "top", "width", "height"])
    if index <= 6:
        sample = []
        physical = radius * density
        for vx in [1, 2, 3, 4, 6, 8, 12]:
            for vy in [1, 2, 3, 4, 6, 8, 12]:
                dx, dy = max(1, round(vx * density)), max(1, round(vy * density))
                distance = math.hypot(dx + 0.5 - physical, dy + 0.5 - physical)
                if dx < physical and dy < physical and abs(distance - physical) < 2:
                    continue
                inside = dx >= physical or dy >= physical or distance < physical
                expected_color = (255, 0, 255) if inside else (255, 255, 255)
                pixel = picture.getpixel((left + dx, top + dy))
                assert all(abs(pixel[i] - expected_color[i]) <= 8 for i in range(3)), (
                    index,
                    phase,
                    radius,
                    [dx, dy],
                    pixel,
                    expected_color,
                )
                sample.append(
                    dict(
                        point=[left + dx, top + dy],
                        pixel=pixel,
                        expected=expected_color,
                    )
                )
        assert len(sample) >= 20
        (f.out / f"{index}-{phase}-corner-pixels.json").write_text(json.dumps(sample))
        print(
            "PASS native rounded-corner mask pixels", index, phase, radius, flush=True
        )
    if 7 <= index <= 12:
        box = (
            left + width // 3,
            top + height + 3,
            left + 2 * width // 3,
            top + height + max(8, round(12 * density)),
        )
        assert (
            0 <= box[0] < box[2] <= picture.width
            and 0 <= box[1] < box[3] <= picture.height
        )
        captures[index] = picture.crop(box)
        captures[index].save(f.out / f"{index}-{phase}-shadow-roi.png")
        if index in [8, 10, 12]:
            off, on = captures[7], captures[index]
            differences = [
                sum(off.getpixel((x, y))) - sum(on.getpixel((x, y)))
                for y in range(off.height)
                for x in range(off.width)
            ]
            dark = sum(d > 6 for d in differences)
            mean = sum(differences) / len(differences) / 3
            assert dark >= 64 and mean > 0.5, (index, dark, mean)
            (f.out / f"{index}-{phase}-shadow-evidence.json").write_text(
                json.dumps(
                    dict(darkenedPixels=dark, meanChannelDarkening=mean, box=box)
                )
            )
        reference = 7 if index in [9, 11] else 8 if index in [10, 12] else None
        if reference is not None:
            diff = ImageChops.difference(captures[reference], captures[index])
            assert max(high for low, high in diff.getextrema()) <= 4, (
                index,
                diff.getextrema(),
            )
        print(
            "PASS actual native/SDK reference shadow pixels", index, phase, flush=True
        )
    f.click("nativeapi-style-next", f"{index}-next")
main_code = re.search(r"NATIVEAPI_STYLE_MAIN_SHADOW_CODE (\d+)", records)
assert main_code
if f.kind == "2in1":
    assert main_code[1] == "0", (
        "Declared normal shadow permission did not enable desktop SDK calls"
    )
if main_code[1] == "0":
    main_captures = {}
    main_rect = None
    main_phases = [
        "main-shadow-off",
        "main-shadow-on",
        "main-reference-off",
        "main-reference-on",
        "main-shadow-off",
    ]
    for index, phase in enumerate(main_phases):
        records = f.wait(f"NATIVEAPI_STYLE_MAIN_PHASE {index} {phase} ")
        line = next(
            line
            for line in records.splitlines()
            if f"NATIVEAPI_STYLE_MAIN_PHASE {index} {phase} " in line
        )
        values = re.search(r"native=(\{.*\}) target=(\{.*\})", line)
        assert values
        state, main = map(json.loads, values.groups())
        assert state["failed"] == 0 and not state["alive"]
        frame = main["windowRect"]
        if main_rect is None:
            main_rect = frame
        assert frame == main_rect and main["id"] != target_id
        rows = f.windows(f"main-{index}-{phase}")
        assert target_id not in rows and int(rows[main["id"]][7]) > 0
        picture = f.screenshot(f"main-{index}-{phase}")
        left, top, width, height = (
            frame[k] for k in ["left", "top", "width", "height"]
        )
        box = (
            left + width // 3,
            top + height + 3,
            left + 2 * width // 3,
            top + height + max(8, round(24 * density)),
        )
        assert (
            0 <= box[0] < box[2] <= picture.width
            and 0 <= box[1] < box[3] <= picture.height
        )
        main_captures[index] = picture.crop(box)
        main_captures[index].save(f.out / f"main-{index}-{phase}-shadow-roi.png")
        if index in [1, 3]:
            off, on = main_captures[0], main_captures[index]
            differences = [
                sum(off.getpixel((x, y))) - sum(on.getpixel((x, y)))
                for y in range(off.height)
                for x in range(off.width)
            ]
            dark = sum(d > 6 for d in differences)
            mean = sum(differences) / len(differences) / 3
            assert dark >= 64 and mean > 0.5, (index, dark, mean)
            (f.out / f"main-{index}-shadow-evidence.json").write_text(
                json.dumps(
                    dict(darkenedPixels=dark, meanChannelDarkening=mean, box=box)
                )
            )
        reference = 0 if index in [2, 4] else 1 if index == 3 else None
        if reference is not None:
            diff = ImageChops.difference(main_captures[reference], main_captures[index])
            assert max(high for low, high in diff.getextrema()) <= 4, (
                index,
                diff.getextrema(),
            )
        print(
            "PASS actual main-window native/SDK reference shadow pixels",
            index,
            phase,
            flush=True,
        )
        f.click("nativeapi-style-next", f"main-{index}-next")
records = f.wait("NATIVEAPI_STYLE_FINAL_PASS")
f.wait("NATIVEAPI_STYLE_UI_COMPLETE")
assert re.search(r"NATIVEAPI_STYLE_FINAL_PASS passed=\d+ failed=0", records)
deadline = time.monotonic() + 10
while target_id in f.windows("closed") and time.monotonic() < deadline:
    time.sleep(0.1)
assert target_id not in f.windows("closed")
print(f"NATIVEAPI_STYLE_SUITE_PASS kind={f.kind} pid={f.pid} evidence={f.out}")
