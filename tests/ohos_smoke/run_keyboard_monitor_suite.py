#!/usr/bin/env python3
"""Authorized guest-only input; real NDK C/C++ monitor lifecycle assertions."""

import argparse
import json
import sys
import time
from pathlib import Path

from ohos_guest_keyboard import PEER, send_shortcut_chord
from ohos_ui_fixture import Fixture


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("deveco", type=Path)
    parser.add_argument("--device", default="127.0.0.1:5555")
    parser.add_argument(
        "--profile", choices=["ordinary", "normal-acl", "system-acl"], required=True
    )
    parser.add_argument("--allow-guest-keyboard", action="store_true", required=True)
    args = parser.parse_args()
    authorized = args.profile == "system-acl"
    bundle = (
        "dev.nativeapi.smoke"
        if args.profile == "ordinary"
        else "dev.nativeapi.keyboard.monitor"
    )
    f = Fixture(args.deveco, args.device, "keyboard-monitor", "MONITOR", bundle)
    assert f.instance["name"] in ["NativeAPI_Phone", "NativeAPI_Desktop"]
    f.wait("NATIVEAPI_MONITOR_READY")
    if f.kind == "2in1":
        dismissed = f.run(
            sys.executable,
            Path(__file__).with_name("notification_ui.py"),
            args.deveco,
            "--device",
            args.device,
            "--pid",
            f.pid,
            "--bundle",
            bundle,
            "--action",
            "dismiss-shortcut",
        )
        (f.out / "installation-prompt.txt").write_text(dismissed)
    raw_metadata = f.guest("bm", "dump", "-n", bundle)
    app = json.loads(raw_metadata[raw_metadata.index("{") :])["applicationInfo"]
    if authorized:
        assert app["appPrivilegeLevel"] == "system_basic" and app["isSystemApp"]
    else:
        assert not app["isSystemApp"]
    commands = 0
    chords = 0
    assertions = 0

    def command(action):
        nonlocal commands
        nonce = str(time.time_ns())
        assert "start ability successfully" in f.guest(
            "aa",
            "start",
            "-a",
            "EntryAbility",
            "-b",
            f.bundle,
            "--ps",
            "nativeapiSuite",
            "keyboard-monitor",
            "--ps",
            "nativeapiKeyboardCommand",
            action,
            "--ps",
            "nativeapiKeyboardRequest",
            nonce,
        )
        marker = f"NATIVEAPI_MONITOR_REPLY {nonce} {action} "
        log = f.wait(marker)
        row = [line.split(marker, 1)[1] for line in log.splitlines() if marker in line][
            -1
        ]
        state = json.loads(row)
        assert state["failed"] == 0, state
        commands += 1
        (f.out / f"{commands:02d}-{action}.json").write_text(
            json.dumps(state, indent=2)
        )
        print(action, state, flush=True)
        return state

    def chord(ctrl=2072, alt=2045, peer=False):
        nonlocal chords
        chords += 1
        send_shortcut_chord(
            f,
            ctrl,
            alt,
            f"monitor-{chords:02d}",
            PEER if peer else bundle,
            approved=args.allow_guest_keyboard,
        )
        return [[1, ctrl], [1, alt], [1, 2098], [2, 2098], [2, alt], [2, ctrl]]

    def verify(expected):
        nonlocal assertions
        state = command("state")
        for actual, wanted in zip(state["events"], expected, strict=True):
            # The guarded driver's unconditional release may produce duplicate
            # up events. It must never introduce extra key-down events.
            assert actual == wanted or (
                len(wanted) == 6 and actual == wanted + wanted[3:]
            ), (actual, wanted)
            assertions += 1
        return state

    setup = command("setup")
    assert setup["permission"] == (0 if authorized else 201), setup
    assert setup["granted"] == (args.profile != "ordinary"), setup
    assertions += 1
    try:
        for ctrl, alt in [(2072, 2045), (2072, 2046), (2073, 2045), (2073, 2046)]:
            command("clear")
            expected = chord(ctrl, alt)
            verify([expected] * 3 if authorized else [[], [], []])
            assert (
                "NATIVEAPI_MONITOR_COMPONENT_F9 type=0 ctrl=true alt=true" in f.logs()
            )
            assertions += 1
        if authorized:
            # A separate UID owns the actual input focus during global delivery.
            f.guest("aa", "force-stop", PEER)
            assert "start ability successfully" in f.guest(
                "aa", "start", "-a", "EntryAbility", "-b", PEER
            )
            time.sleep(0.5)
            peer_pid = f.guest("pidof", PEER).strip()
            metadata = f.guest("bm", "dump", "-n", PEER)
            peer_uid = json.loads(metadata[metadata.index("{") :])["applicationInfo"][
                "uid"
            ]
            assert peer_uid != f.uid and peer_pid != f.pid
            command("clear")
            before_background = f.logs().count(
                "PASS actual native keyboard event delivered on UI"
            )
            expected = chord(peer=True)
            # Capture source delivery before a command returns its focus.
            source_background = f.logs()
            assert (
                source_background.count(
                    "PASS actual native keyboard event delivered on UI"
                )
                - before_background
                == 18
            )
            assertions += 1
            assert "NATIVEAPI_PEER_LOCAL_F9 ctrl=true alt=true" in f.guest(
                "hilog", "-x", "-P", peer_pid
            )
            (f.out / "background-source.log").write_text(source_background)
            verify([expected] * 3)
            command("stop-a")
            command("clear")
            expected = chord()
            verify([[], expected, expected])
            command("worker-stop")
            command("clear")
            chord()
            verify([[], [], []])
            command("worker-start")
            command("clear")
            expected = chord()
            verify([expected] * 3)
            command("remove-a")
            command("clear")
            expected = chord()
            verify([[], expected, expected])
            command("listen-a")
            command("clear")
            command("hold")
            expected = chord()
            verify([[], [], []])
            replaced = command("replace-drain")
            assert replaced["events"][0:2] == [[], []]
            verify([[], [], expected])
            command("clear")
            expected = chord()
            verify([expected, [], expected])
            command("clear")
            command("self-stop")
            expected = chord()
            verify([expected[:3], [], expected])
            command("start-a")
            command("clear")
            command("self-free")
            expected = chord()
            state = verify([expected, [], expected[:3]])
            assert state["released"] == 1 and not state["cActive"]
            assertions += 3
            command("clear")
            command("self-delete")
            expected = chord()
            state = verify([expected[:3], [], []])
            assert not state["aActive"]
            assertions += 1
        command("finish")
        final = command("finish-check")
        assert not final["aActive"] and not final["bActive"] and not final["cActive"]
        assertions += 1
    finally:
        command("finish")
    report = dict(
        guest=f.instance["name"],
        bundle=bundle,
        pid=f.pid,
        uid=f.uid,
        profile=args.profile,
        selfPermission=setup["granted"],
        sdkMonitorResult=setup["permission"],
        apl=app["appPrivilegeLevel"],
        systemApp=app["isSystemApp"],
        chords=chords,
        commands=commands,
        nativeAssertions=final["passed"],
        scenarioAssertions=assertions,
        failed=0,
        evidence=str(f.out),
    )
    (f.out / "summary.json").write_text(json.dumps(report, indent=2))
    print("PASS", json.dumps(report), flush=True)


if __name__ == "__main__":
    main()
