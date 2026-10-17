#!/usr/bin/env python3
"""Ordinary-app native/SDK preferences interoperability and process persistence."""

import argparse
import json
import time
from pathlib import Path

from ohos_ui_fixture import Fixture

ASSERTIONS = {
    "seed": 30,
    "verify": 10,
    "concurrent": 138,
    "limits": 12,
    "clear": 8,
    "empty": 7,
}
BASE = {"unicode": "中文🙂", "empty": "", "key/路径": "line1\nline2=\\n\t🙂"}


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("deveco", type=Path)
    parser.add_argument("--device", default="127.0.0.1:5555")
    args = parser.parse_args()
    f = Fixture(args.deveco, args.device, "preferences", "PREFERENCES")
    assert f.instance["name"] in ["NativeAPI_Desktop", "NativeAPI_Phone"]
    root = f.out
    roots = [str(root)]
    scope = "nativeapi-pref-test-" + str(time.time_ns())
    states = []

    def command(action):
        f.identity()
        metadata = f.guest("bm", "dump", "-n", f.bundle)
        application = json.loads(metadata[metadata.index("{") :])["applicationInfo"]
        assert (
            not application["isSystemApp"]
            and application["appPrivilegeLevel"] == "normal"
        )
        request = str(time.time_ns())
        assert "start ability successfully" in f.guest(
            "aa",
            "start",
            "-a",
            "EntryAbility",
            "-b",
            f.bundle,
            "--ps",
            "nativeapiSuite",
            "preferences",
            "--ps",
            "nativeapiPreferencesScope",
            scope,
            "--ps",
            "nativeapiPreferencesCommand",
            action,
            "--ps",
            "nativeapiPreferencesRequest",
            request,
        )
        marker = f"NATIVEAPI_PREFERENCES_REPLY {request} {action} "
        log = f.wait(marker)
        reply = json.loads(
            next(
                line.split(marker, 1)[1] for line in log.splitlines() if marker in line
            )
        )
        native = json.loads(reply["native"]) if reply["native"] else None
        if native:
            assert native["failed"] == 0 and native["passed"] == ASSERTIONS[action], (
                native
            )
            assert native["values"] == reply["sdk"], reply
        else:
            assert action == "sdk-write" and reply["native"] == "", reply
        expected = {} if action in ["clear", "empty"] else dict(BASE)
        if action not in ["clear", "empty", "seed"]:
            expected["sdk"] = "SDK→native 中文🙂"
        assert reply["sdk"] == expected, reply
        assert reply["maxKey"] == 1024 and reply["maxValue"] == 16 * 1024 * 1024, reply
        state = dict(command=action, pid=f.pid, uid=f.uid, scope=scope, reply=reply)
        index = len(states) + 1
        (root / f"{index:02d}-{action}.json").write_text(json.dumps(state, indent=2))
        (root / f"{index:02d}-{action}.log").write_text(log)
        states.append(state)
        print(action, state, flush=True)
        return reply

    command("seed")
    command("sdk-write")
    command("verify")
    command("concurrent")
    command("limits")
    prior = f.pid
    f = Fixture(args.deveco, args.device, "preferences", "PREFERENCES")
    roots.append(str(f.out))
    assert f.pid != prior
    command("verify")
    command("clear")
    prior = f.pid
    f = Fixture(args.deveco, args.device, "preferences", "PREFERENCES")
    roots.append(str(f.out))
    assert f.pid != prior
    command("empty")
    report = dict(
        guest=f.instance["name"],
        scope=scope,
        pids=sorted({s["pid"] for s in states}),
        rounds=len(states),
        nativeAssertions=sum(
            json.loads(s["reply"]["native"])["passed"]
            for s in states
            if s["reply"]["native"]
        ),
        failed=0,
        roots=roots,
    )
    (root / "summary.json").write_text(json.dumps(report, indent=2))
    print("PASS", json.dumps(report), flush=True)


if __name__ == "__main__":
    main()
