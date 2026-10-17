#!/usr/bin/env python3
"""Window manager identity/lifecycle and SDK order comparison in named guests."""

import argparse
import json
from pathlib import Path

from ohos_ui_fixture import Fixture


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("deveco", type=Path)
    parser.add_argument("--device", default="127.0.0.1:5555")
    args = parser.parse_args()
    f = Fixture(args.deveco, args.device, "window-manager", "MANAGER")
    assert f.instance["name"] in ["NativeAPI_Phone", "NativeAPI_Desktop"]
    log = f.wait("NATIVEAPI_MANAGER_DONE")
    states = []
    for line in log.splitlines():
        if "NATIVEAPI_MANAGER_PHASE " not in line:
            continue
        phase, data = line.split("NATIVEAPI_MANAGER_PHASE ", 1)[1].split(" ", 1)
        state = json.loads(data)
        state["phase"] = phase
        (f.out / f"{phase}.json").write_text(json.dumps(state, indent=2))
        states.append(state)
    assert len(states) == 9, states
    for state in states:
        native = state["native"]
        assert native["failed"] == 0 and native["samples"] == 8, state
        assert native["refreshedUi"] == native["worker"], state
        assert native["passed"] == 132, state
        assert native["worker"]["count"] == state["alive"], state
        assert native["worker"]["current"] == state["focused"], state
        if state["sdkCode"] == 0:
            expected = [
                next((i for i in state["order"] if i != excluded), 0)
                for excluded in [0, *state["system"]]
            ]
            assert native["worker"]["hits"] == expected, state
    supported = [s for s in states if s["sdkCode"] == 0]
    refreshed = sum(
        s["native"]["ui"]["hits"] != s["native"]["worker"]["hits"] for s in supported
    )
    assert len(supported) == 9 and all(s["actionCode"] == 0 for s in states), states
    assert refreshed >= 2, states
    report = dict(
        guest=f.instance["name"],
        pid=f.pid,
        uid=f.uid,
        phases=len(states),
        sdkComparedPhases=len(supported),
        updatedUiSnapshots=refreshed,
        passed=sum(s["native"]["passed"] for s in states),
        failed=0,
        evidence=str(f.out),
    )
    (f.out / "summary.json").write_text(json.dumps(report, indent=2))
    print("PASS", json.dumps(report), flush=True)


if __name__ == "__main__":
    main()
