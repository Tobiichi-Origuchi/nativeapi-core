#!/usr/bin/env python3
"""Primary-window custom shadow pixels, normal SDK refusal and guarded content input."""

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
f = Fixture(a.deveco, a.device, "window-shadow-main", "MAINSHADOW")
metadata = f.guest("bm", "dump", "-n", f.bundle)
app = json.loads(metadata[metadata.index("{") :])["applicationInfo"]
assert not app["isSystemApp"] and app["appPrivilegeLevel"] == "normal"
records = f.wait("NATIVEAPI_MAINSHADOW_REFERENCE ")
reference = json.loads(
    next(
        line.split("NATIVEAPI_MAINSHADOW_REFERENCE ", 1)[1]
        for line in records.splitlines()
        if "NATIVEAPI_MAINSHADOW_REFERENCE " in line
    )
)
assert reference["shadowCode"] == (0 if f.kind == "2in1" else 801), reference
phases = (
    [
        "baseline",
        "disabled-config",
        "soft",
        "hard-red",
        "hard-green",
        "helper-recreated",
        "disabled",
        "reenabled",
        "moved",
        "resized",
        "decorated",
        "decorated-refused",
        "undecorated",
        "cleared",
        "default-off",
        "default-on",
        "default-reference",
        "restored",
    ]
    if f.kind == "2in1"
    else [
        "baseline",
        "refused-cpp",
        "refused-c",
        "refused-ui",
        "refused-clear",
        "restored",
    ]
)
scenes = []
captures = {}
baseline = None
initial_id = None
initial_frame = None
expected_frame = None
for i, name in enumerate(phases):
    marker = f"NATIVEAPI_MAINSHADOW_PHASE {i} {name} "
    records = f.wait(marker)
    state = json.loads(
        next(
            line.split(marker, 1)[1] for line in records.splitlines() if marker in line
        )
    )
    assert state["native"]["failed"] == 0 and state["native"]["alive"]
    main = state["main"]
    initial_id = initial_id or main["id"]
    assert main["id"] == initial_id
    assert all(
        not h["properties"]["focusable"] and not h["properties"]["touchable"]
        for h in state["helpers"]
    )
    row = f.windows(f"{i}-{name}")[initial_id]
    assert row[2] == f.pid and int(row[7]) > 0
    picture = f.screenshot(f"{i}-{name}")
    r = main["windowRect"]
    if initial_frame is None:
        initial_frame = r.copy()
        expected_frame = r.copy()
    if name == "moved":
        expected_frame.update(
            left=initial_frame["left"] + 60, top=initial_frame["top"] + 40
        )
    elif name == "resized":
        expected_frame.update(
            width=initial_frame["width"] + 80, height=initial_frame["height"] + 60
        )
    elif name == "restored":
        expected_frame = initial_frame.copy()
    assert r == expected_frame, (name, r, expected_frame)
    if (
        state["native"]["configured"]
        and state["native"]["enabled"]
        and name not in ["decorated", "decorated-refused"]
    ):
        margin = (
            math.ceil(state["native"]["blur"] * state["density"] / 3) * 3
            + math.ceil(12 * state["density"])
            + 2
        )
        visible = [h for h in state["helpers"] if h["showing"]]
        assert len(visible) == 1
        assert visible[0]["properties"]["windowRect"] == dict(
            left=r["left"] - margin,
            top=r["top"] - margin,
            width=r["width"] + 2 * margin,
            height=r["height"] + 2 * margin,
        )
    center = (r["left"] + r["width"] // 2, r["top"] + r["height"] // 2)
    center_pixel = picture.getpixel(center)
    assert len(center_pixel) == 3
    assert all(abs(center_pixel[j] - (255, 0, 255)[j]) <= 8 for j in range(3))
    if f.kind == "2in1":
        if baseline is None:
            baseline = picture.copy()
        point = (r["left"] + r["width"] + 6, r["top"] + r["height"] // 2)
        assert 0 <= point[0] < picture.width and 0 <= point[1] < picture.height
        pixel = picture.getpixel(point)
        backdrop = baseline.getpixel(point)
        if name in [
            "hard-red",
            "hard-green",
            "helper-recreated",
            "reenabled",
            "moved",
            "resized",
            "undecorated",
        ]:
            channel = 0 if name == "hard-red" else 1
            expected = [
                round(backdrop[j] * 63 / 255 + (192 if j == channel else 0))
                for j in range(3)
            ]
            assert all(abs(pixel[j] - expected[j]) <= 9 for j in range(3)), (
                name,
                pixel,
                expected,
                backdrop,
            )
            assert sum(abs(pixel[j] - backdrop[j]) for j in range(3)) > 70
        elif name == "soft":
            assert (
                pixel[0] > backdrop[0] + 25
                and pixel[1] < backdrop[1] - 10
                and pixel[2] < backdrop[2] - 10
            ), (pixel, backdrop)
        if name in ["cleared", "default-off", "default-on", "default-reference"]:
            box = (
                r["left"] + r["width"] // 3,
                r["top"] + r["height"] + 3,
                r["left"] + 2 * r["width"] // 3,
                r["top"] + r["height"] + 18,
            )
            captures[name] = picture.crop(box)
            captures[name].save(f.out / f"{name}-native-shadow-roi.png")
        state["sample"] = dict(point=point, rgb=pixel, backdrop=backdrop)
    before = len(re.findall(r"NATIVEAPI_MAINSHADOW_CLICK main", f.logs()))
    f.click("nativeapi-main-shadow-content", f"{i}-{name}-content")
    deadline = time.monotonic() + 8
    while time.monotonic() < deadline:
        after = len(re.findall(r"NATIVEAPI_MAINSHADOW_CLICK main", f.logs()))
        if after > before:
            assert after == before + 1
            break
        time.sleep(0.1)
    else:
        raise AssertionError(
            (name, "primary content did not receive actual guarded click")
        )
    scenes.append(state)
    (f.out / f"{i}-{name}.json").write_text(json.dumps(state, indent=2))
    assert "start ability successfully" in f.guest(
        "aa",
        "start",
        "-a",
        "EntryAbility",
        "-b",
        f.bundle,
        "--ps",
        "nativeapiSuite",
        "window-shadow-main",
        "--ps",
        "nativeapiShadowAction",
        str(time.time_ns()),
    )
f.wait("NATIVEAPI_MAINSHADOW_DONE")
if f.kind == "2in1":
    assert (
        max(
            high
            for low, high in ImageChops.difference(
                captures["cleared"], captures["default-reference"]
            ).getextrema()
        )
        <= 8
    )
    assert (
        max(
            high
            for low, high in ImageChops.difference(
                captures["default-on"], captures["default-reference"]
            ).getextrema()
        )
        <= 8
    )
    off, on = captures["default-off"], captures["default-on"]
    darkening = [
        sum(off.getpixel((x, y))) - sum(on.getpixel((x, y)))
        for y in range(off.height)
        for x in range(off.width)
    ]
    assert (
        sum(d > 6 for d in darkening) >= 64
        and sum(darkening) / len(darkening) / 3 > 0.5
    )
summary = dict(
    guest=f.instance["name"],
    pid=f.pid,
    uid=f.uid,
    scenes=len(scenes),
    clicks=len(scenes),
    native=scenes[-1]["native"],
    reference=reference,
    failed=0,
    evidence=str(f.out),
)
(f.out / "summary.json").write_text(json.dumps(summary, indent=2))
print("PASS", json.dumps(summary), flush=True)
