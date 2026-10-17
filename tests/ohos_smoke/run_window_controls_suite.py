#!/usr/bin/env python3
"""Assert actual SDK/native title controls, live C++/C state and deduplicated events."""

import argparse
import json
import re
from pathlib import Path

from ohos_ui_fixture import Fixture

p = argparse.ArgumentParser()
p.add_argument("deveco", type=Path)
p.add_argument("--device", default="127.0.0.1:5555")
a = p.parse_args()
f = Fixture(a.deveco, a.device, "window-controls", "CONTROLS")
records = f.wait("NATIVEAPI_CONTROLS_READY")
ready = re.search(
    r"NATIVEAPI_CONTROLS_READY available=(true|false) main=(\{.*\}) child=(\{.*\})",
    records,
)
assert ready, records[-4000:]
available = ready[1] == "true"
assert available == (f.kind == "2in1")
main, child = map(json.loads, ready.groups()[1:])
base = main["windowRect"]
phases = [
    "baseline",
    "hide",
    "hide-again",
    "show",
    "sdk-hide",
    "sdk-close-only",
    "sdk-max-only",
    "hide-worker",
    "show-ui",
    "hide-decor",
    "show-hidden",
    "show-decor",
    "unlisten-hide",
    "listen-show",
    "child",
    "hidden-maximize",
    "hidden-recover",
    "restore",
]
counts = [3, 0, 0, 3, 0, 1, 1, 0, 3, 0, 3, 3, 0, 3, 3, 0, 0, 3]
events = [0, 1, 1, 2, 3, 4, 4, 5, 6, 7, 8, 8, 8, 9, 9, 10, 10, 11]
f.click("nativeapi-controls-run", "start")
base_events = None
for i, phase in enumerate(phases):
    marker = f"NATIVEAPI_CONTROLS_PHASE {i} {phase} "
    records = f.wait(marker)
    line = next(line for line in records.splitlines() if marker in line)
    match = re.search(r"native=(\{.*\}) target=(\{.*\})", line)
    assert match, line
    state, target = map(json.loads, match.groups())
    assert state["failed"] == 0
    expected = counts[i] if available else 0
    if base_events is None:
        base_events = state["objects"]
    assert state["visible"] == (expected > 0)
    assert (
        state["objects"]
        == state["managers"]
        == state["cEvents"]
        == (base_events + (events[i] if available else 0))
    )
    assert target["id"] == main["id"]
    frame = target["windowRect"]
    if available and phase == "hidden-maximize":
        assert (
            frame["width"] > base["width"] + 100
            and frame["height"] > base["height"] + 100
        )
    else:
        assert frame == base, (phase, frame, base)
    rows = f.windows(phase)
    assert main["id"] in rows and rows[main["id"]][2] == f.pid
    tree = f.tree(phase)
    roots = [
        r
        for r in tree["children"]
        if r["attributes"].get("bundleName") == "dev.nativeapi.smoke"
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
            buttons.append(at)
    assert len(buttons) == expected, (phase, buttons, expected)
    if expected == 1:
        assert (
            ("Close" in buttons[0]["id"])
            if phase == "sdk-close-only"
            else ("Maximize" in buttons[0]["id"])
        )
    (f.out / f"{phase}-controls.json").write_text(json.dumps(buttons))
    f.screenshot(phase)
    print(
        "PASS actual native title controls, live queries and event counts",
        i,
        phase,
        expected,
        flush=True,
    )
    f.click("nativeapi-controls-next", phase + "-next")
records = f.wait("NATIVEAPI_CONTROLS_UI_COMPLETE")
assert "NATIVEAPI_CONTROLS_FINAL_PASS" in records
assert child["id"] not in f.windows("closed")
(f.out / "result.json").write_text(
    json.dumps({"pid": f.pid, "kind": f.kind, "scenes": len(phases)})
)
print(
    f"PASS {len(phases)} native title-control scenes; PID {f.pid}; evidence {f.out}",
    flush=True,
)
