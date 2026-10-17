#!/usr/bin/env python3
"""Real custom title gestures, SDK/WMS geometry and C++/C action results."""

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
f = Fixture(a.deveco, a.device, "window-title", "TITLE")
metadata_text = (f.out / "bundle-metadata.json").read_text()
app = json.loads(metadata_text[metadata_text.index("{") :])["applicationInfo"]
assert app["appPrivilegeLevel"] == "normal" and app["isSystemApp"] is False


def record(marker):
    logs = f.wait(marker)
    return json.loads(
        next(line.split(marker, 1)[1] for line in logs.splitlines() if marker in line)
    )


reference = record("NATIVEAPI_TITLE_REFERENCE ")
available = f.kind == "2in1"
assert reference["available"] == available
assert reference["ordinaryCode"] == 1300004
assert reference["childButtonCode"] == 1300004
ids = [w["id"] for w in reference["snapshot"]["windows"]]
assert len(set(ids)) == 3
bases = [reference["mainBase"], reference["childBase"]]
phases = [
    "baseline",
    "main-disabled-cpp-ui",
    "main-disabled-c-worker",
    "main-cpp-ui-max",
    "main-c-ui-recover",
    "main-cpp-worker-max",
    "main-c-worker-recover",
    "main-overlap-cpp-ui",
    "main-overlap-c-ui-recover",
    "main-maximized-disabled",
    "main-enable-recover",
    "main-fullscreen",
    "main-minimized",
    "main-floating-max",
    "main-final-recover",
    "child-ready",
    "child-cpp-ui-max",
    "child-c-ui-recover",
    "child-cpp-worker-max",
    "child-c-worker-recover",
    "child-overlap-cpp-ui",
    "child-final-recover",
    "ordinary-ui-refusal",
    "ordinary-worker-refusal",
    "closed-child-ui",
    "closed-child-worker",
    "invalid",
    "restore-decor",
    "split-ready",
    "main-split-c-worker-recover",
    "finish",
]


def double_click(target, phase):
    tree = f.tree(phase + "-double-owner")
    identifier = "nativeapi-title-" + ("main" if target == 0 else "child") + "-bar"
    matches = [
        (root, node)
        for root in tree["children"]
        if root["attributes"].get("bundleName") == f.bundle
        for node in f.walk(root)
        if node["attributes"].get("id") == identifier
    ]
    assert len(matches) == 1, (phase, identifier, matches)
    root, node = matches[0]
    assert int(root["attributes"]["hostWindowId"]) == ids[target]
    left, top, right, bottom = f.rect(node)
    assert right > left and bottom > top and node["attributes"]["enabled"] == "true"
    # Prefer the left part of the child bar, outside its floating parent's frame.
    x, y = left + min(30, (right - left) // 4), (top + bottom) // 2
    hits = [
        r
        for r in tree["children"]
        if f.rect(r)[0] <= x < f.rect(r)[2] and f.rect(r)[1] <= y < f.rect(r)[3]
    ]
    assert hits and all(r["attributes"].get("bundleName") == f.bundle for r in hits)
    assert ids[target] in [int(r["attributes"]["hostWindowId"]) for r in hits]
    rows = f.windows(phase + "-double-owner")
    for hit in hits:
        row = rows[int(hit["attributes"]["hostWindowId"])]
        assert row[2] == f.pid and int(row[7]) > 0
    f.identity()
    (f.out / f"{phase}-owner.json").write_text(
        json.dumps(
            dict(pid=f.pid, uid=f.uid, target=ids[target], point=[x, y], hits=hits)
        )
    )
    assert (
        f.guest("uitest", "uiInput", "doubleClick", str(x), str(y)).strip()
        == "No Error"
    )


def split_menu():
    def point(phase, identifier=None, menu=False):
        tree = f.tree(phase)
        matches = (
            [
                (root, node)
                for root in tree["children"]
                if root["attributes"].get("bundleName") == f.bundle
                for node in f.walk(root)
                if (
                    node["attributes"].get("type") == "MenuItem"
                    and node["attributes"].get("text") == "窗口显示在屏幕左侧"
                )
                if menu
            ]
            if menu
            else [
                (root, node)
                for root in tree["children"]
                if root["attributes"].get("bundleName") == f.bundle
                for node in f.walk(root)
                if node["attributes"].get("id") == identifier
            ]
        )
        if menu and not matches:
            return None
        assert len(matches) == 1
        root, node = matches[0]
        left, top, right, bottom = f.rect(node)
        assert right > left and bottom > top and node["attributes"]["enabled"] == "true"
        x, y = (left + right) // 2, (top + bottom) // 2
        hits = [
            r
            for r in tree["children"]
            if f.rect(r)[0] <= x < f.rect(r)[2] and f.rect(r)[1] <= y < f.rect(r)[3]
        ]
        assert (
            len(hits) == 1
            and hits[0]["attributes"]["hostWindowId"]
            == root["attributes"]["hostWindowId"]
        )
        row = f.windows(phase)[int(root["attributes"]["hostWindowId"])]
        assert row[2] == f.pid and int(row[7]) > 0
        f.identity()
        (f.out / f"{phase}-owner.json").write_text(
            json.dumps(
                dict(
                    pid=f.pid,
                    uid=f.uid,
                    point=[x, y],
                    window=row[3],
                    node=node["attributes"],
                )
            )
        )
        return x, y

    x, y = point("split-max-hover", identifier="EnhanceMaximizeBtn")
    answer = f.guest(
        "uinput", "-M", "-m", str(x), str(y), str(x), str(y), "400", "--trace"
    )
    assert not re.search("failed|invalid|error", answer, re.I), answer
    until = time.monotonic() + 6
    chosen = None
    while time.monotonic() < until:
        chosen = point("split-menu-ready", menu=True)
        if chosen is not None:
            break
        time.sleep(0.15)
    assert chosen is not None
    x, y = chosen
    answer = f.guest(
        "uinput", "-M", "-m", str(x), str(y), str(x), str(y), "400", "--trace"
    )
    assert not re.search("failed|invalid|error", answer, re.I), answer
    assert point("split-menu-press", menu=True) == (x, y)
    answer = f.guest("uinput", "-M", "-m", str(x), str(y), "-c", "0")
    assert not re.search("failed|invalid|error", answer, re.I), answer
    time.sleep(0.8)
    f.tree("actual-native-split")
    f.windows("actual-native-split")
    f.screenshot("actual-native-split")


def verify_snapshot(snapshot, phase):
    assert snapshot["native"]["failed"] == 0
    rows = f.windows(phase)
    raw = (f.out / f"{phase}-windows.txt").read_text()
    pattern = (
        r"^(.+?)\s+(\d+)\s+(\d+)\s+(\d+)\s+(\d+)\s+(\d+)\s+(\d+)\s+(-?\d+)\s+"
        r"(\d+)\s+\[\s*(-?\d+)\s+(-?\d+)\s+(\d+)\s+(\d+)\s*\]"
    )
    frames = {
        int(m[4]): tuple(map(int, m.groups()[9:13]))
        for line in raw.splitlines()
        if (m := re.match(pattern, line)) and m[3] == f.pid
    }
    assert len(snapshot["windows"]) == len(snapshot["native"]["windows"]) == 3
    for index in range(3):
        sdk = snapshot["windows"][index]
        native = snapshot["native"]["windows"][index]
        assert len(set(native["events"])) == 1
        if sdk is None:
            assert not native["alive"] and ids[index] not in rows
            assert native["bounds"] == dict(x=0, y=0, width=0, height=0)
            continue
        assert sdk["id"] == ids[index] and native["alive"]
        assert snapshot["freeform"][index] == available
        assert native["maximized"] == (snapshot["status"][index] == 2)
        assert native["minimized"] == (snapshot["status"][index] == 3)
        rect = sdk["windowRect"]
        for key, other in [
            ("x", "left"),
            ("y", "top"),
            ("width", "width"),
            ("height", "height"),
        ]:
            assert (
                abs(native["bounds"][key] * snapshot["density"][index] - rect[other])
                < 0.03
            )
        if snapshot["showing"][index]:
            assert ids[index] in rows and int(rows[ids[index]][7]) > 0
            assert frames[ids[index]] == tuple(
                rect[k] for k in ["left", "top", "width", "height"]
            )
        else:
            assert ids[index] not in rows or int(rows[ids[index]][7]) <= 0


scenes = []
gestures = 0
for i, phase in enumerate(phases):
    wait = record(f"NATIVEAPI_TITLE_WAIT {i} {phase} ")
    target = wait["target"]
    if wait["gesture"]:
        double_click(target, f"{i}-{phase}")
        gesture = record(f"NATIVEAPI_TITLE_GESTURE {i + 1} ")
        assert gesture["lastResult"] == wait["expected"]
        gestures += 1
    state = record(f"NATIVEAPI_TITLE_PHASE {i} {phase} ")
    assert state["gesture"] == wait["gesture"] and state["expected"] == wait["expected"]
    after = state["snapshot"]
    verify_snapshot(after, f"{i}-{phase}")
    before = wait["before"]
    if wait["gesture"]:
        old = before["windows"][target]["windowRect"]
        new = after["windows"][target]["windowRect"]
        old_events = before["native"]["windows"][target]["events"][0]
        new_events = after["native"]["windows"][target]["events"][0]
        if wait["expected"]:
            assert after["status"][target] == state["wanted"]
            if state["wanted"] == 2:
                assert (
                    new["width"] > old["width"] + 100
                    and new["height"] > old["height"] + 100
                )
            else:
                assert new == bases[target]
            assert new_events > old_events, (phase, old_events, new_events)
        else:
            assert new == old and after["status"][target] == before["status"][target]
            assert new_events == old_events
    if available and phase == "main-minimized":
        assert after["status"][0] == 3 and not after["showing"][0]
    if phase == "restore-decor" and available:
        assert after["windows"][0]["drawableRect"]["top"] > 0
    f.screenshot(f"{i}-{phase}")
    (f.out / f"{i}-{phase}.json").write_text(json.dumps(state, indent=2))
    state["before"] = before
    scenes.append(state)
    if phase == "split-ready" and available:
        split_menu()
        # The system's partner picker covers the app. Foreground only our own
        # Ability; the next phase independently requires SDK split status 5.
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
        "window-title",
        "--ps",
        "nativeapiTitleAck",
        str(i + 1),
    )
records = f.wait("NATIVEAPI_TITLE_DONE ")
assert "NATIVEAPI_TITLE_FINAL_PASS" in records
assert gestures == 20
final = scenes[-1]["snapshot"]["native"]
assert final["calls"] == 26 and final["accepted"] == (16 if available else 0)
summary = dict(
    guest=f.instance["name"],
    pid=f.pid,
    uid=f.uid,
    native=final,
    scenes=len(scenes),
    doubleClicks=gestures,
    reference=reference,
    failed=0,
    evidence=str(f.out),
)
(f.out / "summary.json").write_text(json.dumps(summary, indent=2))
print("PASS", json.dumps(summary), flush=True)
