#!/usr/bin/env python3
"""Explicitly authorized, guarded guest-only hotkey input; never host keystrokes."""

import argparse
import json
import time
from pathlib import Path

from ohos_guest_keyboard import send_shortcut_chord
from ohos_ui_fixture import Fixture

PEER = "dev.nativeapi.clipboard.peer"


def main():
    p = argparse.ArgumentParser()
    p.add_argument("deveco", type=Path)
    p.add_argument("--device", default="127.0.0.1:5555")
    p.add_argument("--allow-guest-keyboard", action="store_true", required=True)
    a = p.parse_args()
    f = Fixture(a.deveco, a.device, "shortcuts", "SHORTCUT")
    assert f.instance["name"] in ["NativeAPI_Phone", "NativeAPI_Desktop"], (
        "Only the two explicitly authorized guests"
    )
    f.wait("NATIVEAPI_SHORTCUT_READY")
    sequence = 0

    def command(action):
        nonlocal sequence
        f.identity()
        request = str(time.time_ns())
        result = f.guest(
            "aa",
            "start",
            "-a",
            "EntryAbility",
            "-b",
            "dev.nativeapi.smoke",
            "--ps",
            "nativeapiSuite",
            "shortcuts",
            "--ps",
            "nativeapiShortcutCommand",
            action,
            "--ps",
            "nativeapiShortcutRequest",
            request,
        )
        assert "start ability successfully" in result
        marker = f"NATIVEAPI_SHORTCUT_REPLY {request} {action} "
        deadline = time.monotonic() + 25
        while time.monotonic() < deadline:
            rows = [
                line.split(marker, 1)[1]
                for line in f.logs().splitlines()
                if marker in line
            ]
            if rows:
                value = json.loads(rows[-1])
                assert value["failed"] == 0, value
                sequence += 1
                (f.out / f"{sequence:02d}-{action}.json").write_text(
                    json.dumps(value, indent=2)
                )
                return value
            time.sleep(0.1)
        raise AssertionError(("Missing reply", action))

    def chord(ctrl, alt, bundle="dev.nativeapi.smoke"):
        send_shortcut_chord(
            f, ctrl, alt, f"{sequence:02d}", bundle, approved=a.allow_guest_keyboard
        )
        if bundle == PEER:
            # Read while the peer still owns focus, before any source Want.
            deadline = time.monotonic() + 10
            while time.monotonic() < deadline:
                if (
                    f.logs().count(
                        "PASS real native hotkey callback is delivered on UI"
                    )
                    >= 5
                ):
                    break
                time.sleep(0.1)
            else:
                raise AssertionError("No background delivery while the peer owns focus")

    try:
        command("setup")
        command("register")
        command("input-reference")
        for expected, (ctrl, alt) in enumerate(
            [(2072, 2045), (2072, 2046), (2073, 2045), (2073, 2046)], 1
        ):
            chord(ctrl, alt)
            state = command("state")
            assert (
                state["callbacks"] == state["activated"] == expected
                and state["lastFocused"]
                and state["sdkCallbacks"] == expected
            ), state
        chord(2072, 2045, PEER)
        state = command("state")
        assert (
            state["callbacks"] == state["activated"] == 5
            and not state["lastFocused"]
            and state["sdkCallbacks"] == 5
        ), state
        for expected_sdk, (disable, enable) in enumerate(
            [
                ("disable-manager", "enable-manager"),
                ("disable-shortcut", "enable-shortcut"),
            ],
            6,
        ):
            command(disable)
            chord(2073, 2046)
            state = command("state")
            assert (
                state["callbacks"] == state["activated"] == 5
                and state["sdkCallbacks"] == expected_sdk
            ), state
            command(enable)
        command("self-remove")
        chord(2072, 2045)
        state = command("state")
        assert (
            state["callbacks"] == state["activated"] == 6
            and state["count"] == 0
            and state["sdkCallbacks"] == 8
        ), state
        chord(2072, 2045)
        state = command("state")
        assert (
            state["callbacks"] == state["activated"] == 6 and state["sdkCallbacks"] == 9
        ), state
        command("finish")
        result = dict(pid=f.pid, kind=f.kind, state=state)
        (f.out / "input-result.json").write_text(json.dumps(result, indent=2))
        f.screenshot("input-complete")
        print("NATIVEAPI_SHORTCUT_INPUT_PASS", result, "evidence", f.out, flush=True)
    finally:
        command("finish")


if __name__ == "__main__":
    main()
