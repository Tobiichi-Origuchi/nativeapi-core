"""Only the explicitly approved Ctrl+Alt+F9 chord, on the two named guests.

Importing this module sends no input. Every call rechecks instance, process,
UID and real WindowManager focus, then releases the chord in finally.
"""

import json
import re
import time

PEER = "dev.nativeapi.clipboard.peer"


def send_shortcut_chord(
    f, ctrl, alt, label, bundle="dev.nativeapi.smoke", *, approved=False
):
    assert approved, "Explicit guest keyboard authorization is required"
    assert ctrl in [2072, 2073] and alt in [2045, 2046]
    assert bundle in ["dev.nativeapi.smoke", "dev.nativeapi.keyboard.monitor", PEER]
    f.identity()
    assert "start ability successfully" in f.guest(
        "aa", "start", "-a", "EntryAbility", "-b", bundle
    )
    time.sleep(0.5)
    pid = f.guest("pidof", bundle).strip()
    assert re.fullmatch(r"\d+", pid)
    assert f.guest("cat", f"/proc/{pid}/cmdline").split("\0")[0] == bundle
    metadata = f.guest("bm", "dump", "-n", bundle)
    uid = json.loads(metadata[metadata.index("{") :])["applicationInfo"]["uid"]
    status = f.guest("cat", f"/proc/{pid}/status")
    found = re.search(r"^Uid:\s+(\d+)\s+(\d+)\s+(\d+)\s+(\d+)", status, re.M)
    assert found and all(int(n) == uid for n in found.groups())
    instances = [
        i
        for i in json.loads(f.run(f.emulator, "-list", "-details"))
        if i["isRunning"] == "true"
    ]
    assert len(instances) == 1 and instances[0]["name"] == f.instance["name"]
    assert instances[0]["name"] in ["NativeAPI_Phone", "NativeAPI_Desktop"]
    windows = f.guest("hidumper", "-s", "WindowManagerService", "-a", "-a")
    focus = re.search(r"^Focus window:\s*(\d+)\s*$", windows, re.M)
    assert focus, windows[-2000:]
    win_id = focus.group(1)
    assert any(
        re.match(r"^\S+\s+\d+\s+" + pid + r"\s+" + win_id + r"\s+", row)
        for row in windows.splitlines()
    ), "The own fixture must own the actual guest focus"
    label = f"{label}-{ctrl}-{alt}-{bundle}"
    (f.out / (label + "-focus.txt")).write_text(windows)
    (f.out / (label + "-owner.json")).write_text(
        json.dumps(
            dict(
                guest=f.instance["name"],
                bundle=bundle,
                pid=pid,
                uid=uid,
                focusedWindow=win_id,
                keys=[ctrl, alt, 2098],
            ),
            indent=2,
        )
    )
    # One bounded guest command presses/releases the complete chord. An
    # unconditional second release handles a failed transport/tool exit.
    try:
        result = f.guest(
            "uinput",
            "-K",
            "-d",
            str(ctrl),
            "-d",
            str(alt),
            "-d",
            "2098",
            "-u",
            "2098",
            "-u",
            str(alt),
            "-u",
            str(ctrl),
        )
        assert not re.search("failed|invalid|error", result, re.I), result
    finally:
        f.guest("uinput", "-K", "-u", "2098", "-u", str(alt), "-u", str(ctrl))
    time.sleep(0.5)
    (f.out / (label + "-native.log")).write_text(f.logs())
