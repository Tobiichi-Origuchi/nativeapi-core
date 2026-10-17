#!/usr/bin/env python3
"""System caption policies, property events and programmatic actions."""

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
f = Fixture(a.deveco, a.device, "window-policies", "POLICY")
metadata_text = (f.out / "bundle-metadata.json").read_text()
application = json.loads(metadata_text[metadata_text.index("{") :])["applicationInfo"]
assert application["appPrivilegeLevel"] == "normal"
assert application["isSystemApp"] is False
records = f.wait("NATIVEAPI_POLICY_REFERENCE ")
reference = json.loads(
    next(
        line.split("NATIVEAPI_POLICY_REFERENCE ", 1)[1]
        for line in records.splitlines()
        if "NATIVEAPI_POLICY_REFERENCE " in line
    )
)
available = f.kind == "2in1"
assert reference["mainCode"] == (0 if available else 801)
assert reference["childCode"] == 1300004
phases = [
    "baseline",
    "min-off",
    "min-off-again",
    "max-off",
    "close-off",
    "max-hidden-double-click",
    "recover-after-hidden-double",
    "program-max",
    "program-recover",
    "close-on",
    "collective-hide",
    "min-on-hidden",
    "collective-show",
    "hide-decor",
    "max-on-hidden",
    "show-decor",
    "sdk-close-only",
    "max-off-after-sdk",
    "min-off-second",
    "max-on",
    "user-maximize",
    "recover-after-user",
    "enabled-double-click",
    "recover-after-double",
    "min-off-repeat",
    "program-minimize",
    "program-restore",
    "policy-child",
    "close-off-final",
    "close-child",
    "main-close-refused",
    "finish",
]
policy = [available] * 3
shown = True
events = None
base = None
scenes = []
for i, name in enumerate(phases):
    marker = f"NATIVEAPI_POLICY_PHASE {i} {name} "
    logs = f.wait(marker)
    state = json.loads(
        next(line.split(marker, 1)[1] for line in logs.splitlines() if marker in line)
    )
    current = state["native"]
    assert current["failed"] == 0
    if events is None:
        events = current["policyObjects"].copy()
    changes = {
        "min-off": (0, False),
        "min-off-again": (0, False),
        "max-off": (1, False),
        "close-off": (2, False),
        "close-on": (2, True),
        "min-on-hidden": (0, True),
        "max-on-hidden": (1, True),
        "max-off-after-sdk": (1, False),
        "min-off-second": (0, False),
        "max-on": (1, True),
        "min-off-repeat": (0, False),
        "close-off-final": (2, False),
    }
    if name in changes and available:
        index, value = changes[name]
        if policy[index] != value:
            policy[index] = value
            events[index] += 1
    if name in ["collective-hide", "hide-decor"]:
        shown = False
    if name in ["collective-show", "show-decor"]:
        shown = True
    assert current["policy"] == policy, (name, current, policy)
    assert current["childPolicy"] == [False] * 3
    assert (
        current["policyObjects"]
        == current["policyManagers"]
        == current["policyC"]
        == events
    ), (name, current, events)
    main = state["main"]
    rect = main["windowRect"]
    if base is None:
        base = rect.copy()
    if available and name == "program-max":
        assert (
            state["status"] == 2
            and rect["width"] > base["width"] + 100
            and rect["height"] > base["height"] + 100
        )
    else:
        assert rect == base, (name, rect, base)
    rows = f.windows(f"{i}-{name}")
    if available and name == "program-minimize":
        assert state["status"] == 3
        assert main["id"] not in rows or int(rows[main["id"]][7]) <= 0
    else:
        assert rows[main["id"]][2] == f.pid and int(rows[main["id"]][7]) > 0
        tree = f.tree(f"{i}-{name}")
        roots = [
            r
            for r in tree["children"]
            if r["attributes"].get("bundleName") == f.bundle
            and int(r["attributes"]["hostWindowId"]) == main["id"]
        ]
        assert len(roots) == 1
        buttons = []
        for node in f.walk(roots[0]):
            at = node["attributes"]
            left, top, right, bottom = f.rect(node)
            if (
                re.fullmatch(
                    r"Enhance(?:Maximize|Recover|Restore|Minimize|Close)Btn",
                    at.get("id", ""),
                )
                and at.get("visible") == "true"
                and float(at.get("opacity", "1")) > 0
                and right > left
                and bottom > top
            ):
                buttons.append(at["id"])
        expected = []
        if available and shown:
            if name == "sdk-close-only":
                expected = ["Close"]
            else:
                expected = [
                    n
                    for j, n in enumerate(["Minimize", "Maximize", "Close"])
                    if policy[j]
                ]
        actual = sorted(
            re.sub(r"^Enhance|Btn$", "", n)
            .replace("Recover", "Maximize")
            .replace("Restore", "Maximize")
            for n in buttons
        )
        assert actual == sorted(expected), (name, actual, expected)
        assert current["visible"] == bool(expected)
        state["actualButtons"] = buttons
    if available and name in ["max-hidden-double-click", "enabled-double-click"]:
        x = rect["left"] + rect["width"] // 2
        y = rect["top"] + main["drawableRect"]["top"] // 2
        assert main["drawableRect"]["top"] > 10
        tree = f.tree(f"{i}-{name}-double-owner")
        hits = [
            r
            for r in tree["children"]
            if f.rect(r)[0] <= x < f.rect(r)[2] and f.rect(r)[1] <= y < f.rect(r)[3]
        ]
        assert len(hits) == 1 and hits[0]["attributes"].get("bundleName") == f.bundle
        assert int(hits[0]["attributes"]["hostWindowId"]) == main["id"]
        f.identity()
        rows = f.windows(f"{i}-{name}-double-owner")
        assert rows[main["id"]][2] == f.pid and int(rows[main["id"]][7]) > 0
        (f.out / f"{name}-owner.json").write_text(
            json.dumps(dict(point=[x, y], pid=f.pid, uid=f.uid, window=main["id"]))
        )
        output = f.guest("uitest", "uiInput", "doubleClick", str(x), str(y))
        assert output.strip() == "No Error", output
        # Observe the gesture before acknowledging it: this prevents a following
        # programmatic maximize from hiding the SDK's independent title gesture.
        until = time.monotonic() + 1.2
        while time.monotonic() < until:
            f.identity()
            time.sleep(0.12)
    f.screenshot(f"{i}-{name}")
    (f.out / f"{i}-{name}.json").write_text(json.dumps(state, indent=2))
    scenes.append(state)
    if name == "user-maximize" and available:
        f.click("EnhanceMaximizeBtn", "real-system-maximize")
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
        "window-policies",
        "--ps",
        "nativeapiPolicyAction",
        str(time.time_ns()),
    )
    if (
        name in ["user-maximize", "enabled-double-click", "max-hidden-double-click"]
        and available
    ):
        gesture_marker = f"NATIVEAPI_POLICY_GESTURE {name} "
        log = f.wait(gesture_marker)
        maximized = json.loads(
            next(
                line.split(gesture_marker, 1)[1]
                for line in log.splitlines()
                if gesture_marker in line
            )
        )
        # Caption visibility cannot suppress the SDK's native double click. Keep
        # this limitation observable; do not count it as user-maximize denial.
        assert maximized["status"] == 2
        assert maximized["main"]["windowRect"]["width"] > base["width"] + 100
        (f.out / f"actual-gesture-{name}.json").write_text(
            json.dumps(maximized, indent=2)
        )
records = f.wait("NATIVEAPI_POLICY_DONE ")
assert "NATIVEAPI_POLICY_FINAL_PASS" in records
assert scenes[-1]["native"]["policy"][2] is False
f.identity()
assert f.windows("after-programmatic-child-close")[reference["main"]["id"]][2] == f.pid
assert reference["child"]["id"] not in f.windows("closed-child")
summary = dict(
    guest=f.instance["name"],
    pid=f.pid,
    uid=f.uid,
    scenes=len(scenes),
    native=scenes[-1]["native"],
    systemMaximizeClicks=1 if available else 0,
    guardedTitleDoubleClicks=2 if available else 0,
    primaryCloseRefused=True,
    programmaticChildClose=True,
    reference=reference,
    failed=0,
    evidence=str(f.out),
)
(f.out / "summary.json").write_text(json.dumps(summary, indent=2))
print("PASS", json.dumps(summary), flush=True)
