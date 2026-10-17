#!/usr/bin/env python3
"""Compare reparent requests and live native/C getters with real SDK windows."""

import argparse
import json
from pathlib import Path

from ohos_ui_fixture import Fixture


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("deveco", type=Path)
    parser.add_argument("--device", default="127.0.0.1:5555")
    args = parser.parse_args()
    f = Fixture(args.deveco, args.device, "window-parent", "PARENT")
    assert f.instance["name"] in ["NativeAPI_Phone", "NativeAPI_Desktop"]
    records = f.wait("NATIVEAPI_PARENT_DONE")
    phases = []
    for line in records.splitlines():
        if "NATIVEAPI_PARENT_PHASE " not in line:
            continue
        name, payload = line.split("NATIVEAPI_PARENT_PHASE ", 1)[1].split(" ", 1)
        data = json.loads(payload)
        assert data["native"]["failed"] == data["observed"]["failed"] == 0
        assert data["sdk"] == data["observed"]["parents"] == data["live"]["parents"]
        (f.out / f"{name}.json").write_text(json.dumps(data, indent=2))
        phases.append(name)
    expected = [
        "initial",
        "invalid-ui",
        "invalid-worker",
        "ui-to-a",
        "cycle",
        "c-worker-to-b",
        "external-to-a",
        "external-cycle",
        "worker-to-main",
        "c-ui-to-main",
        "same-parent",
        "pending",
        "retry-after-pending",
        "old-parent-destroyed",
        "dead-parent",
        "active-parent-destroyed",
        "closed-child",
    ]
    assert phases == expected, phases
    done = next(
        line for line in records.splitlines() if "NATIVEAPI_PARENT_DONE " in line
    )
    summary = json.loads(done.split("NATIVEAPI_PARENT_DONE ", 1)[1])
    assert (
        summary["phases"] == 17 and summary["passed"] == 420 and summary["failed"] == 0
    )
    if f.kind == "2in1":
        assert summary["sdkCode"] == 0
    assert summary["sdkCode"] in [0, 801]
    summary.update(guest=f.instance["name"], pid=f.pid, uid=f.uid, evidence=str(f.out))
    (f.out / "summary.json").write_text(json.dumps(summary, indent=2))
    print("PASS", json.dumps(summary), flush=True)


if __name__ == "__main__":
    main()
