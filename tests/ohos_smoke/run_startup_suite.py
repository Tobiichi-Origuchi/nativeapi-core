#!/usr/bin/env python3
"""Read-only launch-at-login parity with the real public SDK, without keys."""

import argparse
import json
import time
from pathlib import Path

from ohos_ui_fixture import Fixture


def check(f, request, expected=None):
    marker = f"NATIVEAPI_STARTUP_REPLY {request} "
    logs = f.wait(marker)
    state = json.loads(
        next(line.split(marker, 1)[1] for line in logs.splitlines() if marker in line)
    )
    ref_marker = f"NATIVEAPI_STARTUP_REFERENCE {request} "
    reference = json.loads(
        next(
            line.split(ref_marker, 1)[1]
            for line in logs.splitlines()
            if ref_marker in line
        )
    )
    assert state["failed"] == 0 and state["passed"] == 25, state
    assert reference["code"] in [0, 801] and state["expected"] == reference["enabled"]
    assert expected is None or state["expected"] == expected, (state, expected)
    (f.out / (request + "-state.json")).write_text(
        json.dumps(dict(native=state, sdk=reference), indent=2)
    )
    return state, reference


def refresh(f, expected=None):
    request = str(time.time_ns())
    assert "start ability successfully" in f.guest(
        "aa",
        "start",
        "-a",
        "EntryAbility",
        "-b",
        "dev.nativeapi.smoke",
        "--ps",
        "nativeapiSuite",
        "startup",
        "--ps",
        "nativeapiStartupRequest",
        request,
    )
    return check(f, request, expected)


def main():
    p = argparse.ArgumentParser()
    p.add_argument("deveco", type=Path)
    p.add_argument("--device", default="127.0.0.1:5555")
    p.add_argument("--expected", choices=["true", "false", "auto"], default="auto")
    p.add_argument("--settings-transitions", action="store_true")
    p.add_argument("--repeats", type=int, default=3)
    a = p.parse_args()
    assert 1 <= a.repeats <= 8
    expected = None if a.expected == "auto" else a.expected == "true"
    f = Fixture(a.deveco, a.device, "startup", "STARTUP")
    states = [check(f, "initial", expected)]
    for _ in range(a.repeats - 1):
        states.append(refresh(f, expected))
    if a.settings_transitions:
        from startup_settings import StartupSettings

        settings = StartupSettings(f)
        assert f.kind == "2in1", "Desktop startup-list Settings transitions only"
        original = states[-1][0]["expected"]
        try:
            for value in [not original, original, not original, original]:
                settings.desktop(value)
                states.append(refresh(f, value))
        finally:
            settings.desktop(original)
            refresh(f, original)
    f.screenshot("startup-complete")
    result = dict(
        pid=f.pid,
        uid=f.uid,
        kind=f.kind,
        rounds=len(states),
        passed=sum(x[0]["passed"] for x in states),
        failed=0,
        expected=states[-1][0]["expected"],
        sdk=states[-1][1],
    )
    (f.out / "result.json").write_text(json.dumps(result, indent=2))
    print("NATIVEAPI_STARTUP_PASS", result, "evidence", f.out, flush=True)


if __name__ == "__main__":
    main()
