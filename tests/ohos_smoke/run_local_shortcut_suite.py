#!/usr/bin/env python3
"""Actual window-local input, C/C++ lifecycle, and independent global ownership."""

import argparse
import json
import re
import time
from pathlib import Path

from ohos_guest_keyboard import PEER, send_shortcut_chord
from ohos_ui_fixture import Fixture


def main():
    p = argparse.ArgumentParser()
    p.add_argument("deveco", type=Path)
    p.add_argument("--device", default="127.0.0.1:5555")
    p.add_argument("--allow-guest-keyboard", action="store_true", required=True)
    a = p.parse_args()
    f = Fixture(a.deveco, a.device, "shortcuts", "SHORTCUT")
    assert f.instance["name"] in ["NativeAPI_Phone", "NativeAPI_Desktop"]
    f.wait("NATIVEAPI_SHORTCUT_READY")
    f.guest("aa", "force-stop", PEER)
    assert "start ability successfully" in f.guest(
        "aa", "start", "-a", "EntryAbility", "-b", PEER
    )
    time.sleep(0.5)
    peer_pid = f.guest("pidof", PEER).strip()
    assert re.fullmatch(r"\d+", peer_pid) and peer_pid != f.pid
    metadata = f.guest("bm", "dump", "-n", PEER)
    (f.out / "peer-metadata.txt").write_text(metadata)
    peer_uid = json.loads(metadata[metadata.index("{") :])["applicationInfo"]["uid"]
    assert peer_uid != f.uid
    sequence = 0
    chords = 0

    def command(action, peer=False):
        nonlocal sequence
        f.identity()
        assert f.guest("pidof", PEER).strip() == peer_pid
        assert f.guest("cat", f"/proc/{peer_pid}/cmdline").split("\0")[0] == PEER
        status = f.guest("cat", f"/proc/{peer_pid}/status")
        uid = re.search(r"^Uid:\s+(\d+)\s+(\d+)\s+(\d+)\s+(\d+)", status, re.M)
        assert uid and all(int(v) == peer_uid for v in uid.groups())
        nonce = str(time.time_ns())
        params = (
            ["--ps", "command", action, "--ps", "request", nonce]
            if peer
            else [
                "--ps",
                "nativeapiSuite",
                "shortcuts",
                "--ps",
                "nativeapiShortcutCommand",
                action,
                "--ps",
                "nativeapiShortcutRequest",
                nonce,
            ]
        )
        assert "start ability successfully" in f.guest(
            "aa",
            "start",
            "-a",
            "EntryAbility",
            "-b",
            PEER if peer else "dev.nativeapi.smoke",
            *params,
        )
        marker = (
            ("NATIVEAPI_SHORTCUT_PEER_REPLY " if peer else "NATIVEAPI_SHORTCUT_REPLY ")
            + nonce
            + " "
            + action
            + " "
        )
        deadline = time.monotonic() + 20
        while time.monotonic() < deadline:
            log = f.guest("hilog", "-x", "-P", peer_pid) if peer else f.logs()
            rows = [
                line.split(marker, 1)[1] for line in log.splitlines() if marker in line
            ]
            if rows:
                result = json.loads(rows[-1])
                assert result.get("failed", 0) == 0, result
                sequence += 1
                label = f"{sequence:02d}-" + ("peer-" if peer else "") + action
                (f.out / (label + ".json")).write_text(json.dumps(result, indent=2))
                (f.out / (label + ".log")).write_text(log)
                print(label, result, flush=True)
                return result
            time.sleep(0.1)
        raise AssertionError(("Missing reply", action))

    def chord(ctrl=2072, alt=2045, peer=False):
        nonlocal chords
        chords += 1
        send_shortcut_chord(
            f,
            ctrl,
            alt,
            f"local-{chords:02d}",
            PEER if peer else "dev.nativeapi.smoke",
            approved=a.allow_guest_keyboard,
        )

    def counts(expected):
        s = command("state")
        assert s["callbacks"] == s["activated"] == expected, s
        return s

    try:
        command("setup")  # Includes refusal before a host exists.
        command("local-host-attach")
        command("local-register")
        for n, pair in enumerate(
            [(2072, 2045), (2072, 2046), (2073, 2045), (2073, 2046)], 1
        ):
            chord(*pair)
            assert counts(n)["lastFocused"]
        # A normal focused peer receives the real unreserved key event. Check
        # the source before any source Want can change guest focus.
        chord(peer=True)
        peer_log = f.guest("hilog", "-x", "-P", peer_pid)
        (f.out / "peer-local-input.log").write_text(peer_log)
        assert "NATIVEAPI_PEER_LOCAL_F9 ctrl=true alt=true" in peer_log
        assert f.logs().count("PASS actual local callback runs on focused UI") == 4
        counts(4)
        # A different UID can own all four global variants while ours is local.
        acquired = command("hotkey-acquire", peer=True)
        assert acquired["code"] == 0 and acquired["held"] == 4, acquired
        chord(peer=True)
        state = command("hotkey-state", peer=True)
        assert state["callbacks"] == 1, state
        # Inspect the source log before a Want can bring it to the foreground.
        assert f.logs().count("PASS actual local callback runs on focused UI") == 4
        counts(4)
        command("local-release")
        # Register after a different app already owns the global chord. A
        # global availability probe here would incorrectly reject this.
        command("local-register")
        assert command("hotkey-state", peer=True)["held"] == 4
        released = command("hotkey-release", peer=True)
        assert released["code"] == 0 and released["held"] == 0
        for disable, enable in [
            ("disable-manager", "enable-manager"),
            ("disable-shortcut", "enable-shortcut"),
        ]:
            command(disable)
            before = f.logs().count(
                "NATIVEAPI_LOCAL_KEY key=2098 type=0 modifiers=6 consumed=0"
            )
            chord(2073, 2046)
            assert (
                f.logs().count(
                    "NATIVEAPI_LOCAL_KEY key=2098 type=0 modifiers=6 consumed=0"
                )
                == before + 1
            )
            counts(4)
            command(enable)
        command("local-host-detach")
        chord()
        counts(4)
        command("local-host-attach")
        chord()
        counts(5)
        command("self-remove")
        chord()
        assert counts(6)["count"] == 0
        chord()
        counts(6)
        command("local-register-c")
        chord(2073, 2046)
        counts(7)
        command("local-release")
        chord()
        assert counts(7)["released"] == 1
        command("local-worker")
        chord()
        counts(8)
        command("local-release")
        command("local-register")
        command("local-event-remove")
        chord()
        final = command("state")
        assert (
            final["callbacks"] == 8 and final["activated"] == 9 and final["count"] == 0
        )
        command("local-host-detach")
        command("finish")
        final = command("state")
        assert (
            final["callbacks"] == 8 and final["activated"] == 9 and final["count"] == 0
        )
        result = dict(
            pid=f.pid,
            peerPid=peer_pid,
            uid=f.uid,
            peerUid=peer_uid,
            kind=f.kind,
            chords=chords,
            state=final,
        )
        (f.out / "local-result.json").write_text(json.dumps(result, indent=2))
        f.screenshot("local-complete")
        print("NATIVEAPI_LOCAL_SHORTCUT_PASS", result, "evidence", f.out, flush=True)
    finally:
        command("hotkey-release", peer=True)
        command("local-host-detach")
        command("finish")


if __name__ == "__main__":
    main()
