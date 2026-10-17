#!/usr/bin/env python3
"""Verify actual app window groups, SDK badges and rendered mission icons."""

import argparse
import json
import os
import re
import subprocess
import time
from pathlib import Path

from PIL import Image

p = argparse.ArgumentParser()
p.add_argument("deveco", type=Path)
p.add_argument("--pid", type=int, required=True)
p.add_argument("--device", default="127.0.0.1:5555")
a = p.parse_args()
assert re.fullmatch(r"127\.0\.0\.1:\d+", a.device)
suffix = ".exe" if os.name == "nt" else ""
hdc = a.deveco / f"sdk/default/openharmony/toolchains/hdc{suffix}"
emulator = a.deveco / f"tools/emulator/Emulator{suffix}"
out = Path(__file__).resolve().parent / "test-results/app-integration-ui" / str(a.pid)
out.mkdir(parents=True, exist_ok=True)


def run(*cmd):
    return subprocess.check_output(
        list(map(str, cmd)),
        stdin=subprocess.DEVNULL,
        text=True,
        encoding="utf-8",
        stderr=subprocess.STDOUT,
        timeout=35,
    )


def guest(*cmd):
    return run(hdc, "-t", a.device, "shell", *cmd)


assert guest("param", "get", "const.product.model").strip() == "emulator"
kind = guest("param", "get", "const.product.devicetype").strip()
instances = [
    i
    for i in json.loads(run(emulator, "-list", "-details"))
    if i["isRunning"] == "true"
]
assert len(instances) == 1 and instances[0]["name"] == (
    "NativeAPI_Desktop" if kind == "2in1" else "NativeAPI_Phone"
)
instance = instances[0]
metadata = guest("bm", "dump", "-n", "dev.nativeapi.smoke")
uid = json.loads(metadata[metadata.index("{") :])["applicationInfo"]["uid"]


def identity():
    assert guest("pidof", "dev.nativeapi.smoke").strip() == str(a.pid)
    assert (
        guest("cat", f"/proc/{a.pid}/cmdline").split("\0")[0] == "dev.nativeapi.smoke"
    )
    found = re.search(
        r"^Uid:\s+(\d+)\s+(\d+)\s+(\d+)\s+(\d+)",
        guest("cat", f"/proc/{a.pid}/status"),
        re.M,
    )
    assert found and all(int(v) == uid for v in found.groups())


def walk(n):
    yield n
    for c in n.get("children", []):
        yield from walk(c)


def rect(n):
    b = tuple(map(int, re.findall(r"-?\d+", n["attributes"]["bounds"])))
    assert len(b) == 4
    return b


def tree(phase):
    identity()
    remote = "/data/local/tmp/nativeapi-app-integrations.json"
    assert "DumpLayout saved" in guest("uitest", "dumpLayout", "-p", remote)
    local = out / f"{phase}-{time.time_ns()}.json"
    run(hdc, "-t", a.device, "file", "recv", remote, local)
    return json.loads(local.read_text())


def own_roots(t):
    return [
        r
        for r in t["children"]
        if r["attributes"].get("bundleName") == "dev.nativeapi.smoke"
    ]


def main_root(t):
    matches = [
        r
        for r in own_roots(t)
        if any(
            n["attributes"].get("text")
            == "NativeAPI Application Integration Acceptance"
            for n in walk(r)
        )
    ]
    assert len(matches) == 1
    return matches[0]


def window_rows(native):
    rows = []
    for line in native.splitlines():
        match = re.match(
            r"^(.+?)\s+(\d+)\s+(\d+)\s+(\d+)\s+(\d+)\s+(\d+)\s+(\d+)\s+(-?\d+)\s+",
            line,
        )
        if match:
            rows.append(match.groups())
    return rows


def owned_point(t, r, x, y, phase, node):
    identity()
    hits = [
        q
        for q in t["children"]
        if rect(q)[0] <= x < rect(q)[2] and rect(q)[1] <= y < rect(q)[3]
    ]
    assert (
        len(hits) == 1
        and hits[0]["attributes"]["hostWindowId"] == r["attributes"]["hostWindowId"]
    )
    assert r["attributes"].get("bundleName") == "dev.nativeapi.smoke"
    wid = r["attributes"]["hostWindowId"]
    windows = guest("hidumper", "-s", "WindowManagerService", "-a", "-a")
    rows = [row for row in window_rows(windows) if row[3] == wid]
    assert len(rows) == 1 and int(rows[0][2]) == a.pid and int(rows[0][7]) > 0
    (out / f"{phase}-owner.json").write_text(
        json.dumps(
            dict(pid=a.pid, uid=uid, window=wid, point=[x, y], node=node["attributes"])
        )
    )
    (out / f"{phase}-windows.txt").write_text(windows)
    return x, y


def owned(t, r, n, phase):
    left, top, right, bottom = rect(n)
    assert right > left and bottom > top
    return owned_point(t, r, (left + right) // 2, (top + bottom) // 2, phase, n)


def click(identifier):
    t = tree(identifier)
    r = main_root(t)
    m = [n for n in walk(r) if n["attributes"].get("id") == identifier]
    assert len(m) == 1 and m[0]["attributes"].get("enabled") == "true"
    x, y = owned(t, r, m[0], identifier)
    result = run(emulator, "-instance", instance["name"], "-click", f"{x} {y}")
    assert "Scenario simulation success" in result, result


def logs():
    text = guest("hilog", "-x", "-P", a.pid)
    (out / "native.log").write_text(text)
    assert not re.search(r"\bFAIL\b|NATIVEAPI_INTEGRATION_ERROR", text), text[-10000:]
    return text


def wait(marker, count=1):
    deadline = time.monotonic() + 35
    while time.monotonic() < deadline:
        identity()
        text = logs()
        if text.count(marker) >= count:
            return text
        time.sleep(0.2)
    raise AssertionError("Missing " + marker)


def badge_nodes(expected):
    t = tree("badge-" + str(expected))
    found = []
    for r in t["children"]:
        if r["attributes"].get("bundleName") != "com.ohos.sceneboard":
            continue
        for node in walk(r):
            at = node["attributes"]
            identifier = at.get("id", "")
            if "dev.nativeapi.smoke" not in identifier:
                continue
            for child in walk(node):
                q = child["attributes"]
                if q.get("text") in ["9", "99+"] or "badge" in q.get("id", "").lower():
                    found.append(q)
    (out / f"badge-{expected}-system-nodes.json").write_text(
        json.dumps(found, ensure_ascii=False)
    )
    if kind == "2in1":
        label = "9" if expected == 9 else "99+" if expected == 100 else ""
        if label:
            assert any(
                n.get("text") == label and n.get("id", "").startswith("CustomBadge_")
                for n in found
            ), found
        else:
            assert not found, found
    print(
        "SYSTEM BADGE",
        expected,
        [(n.get("id"), n.get("text")) for n in found],
        flush=True,
    )
    return found


def phone_home():
    def launcher(t):
        return not own_roots(t) and any(
            r["attributes"].get("bundleName") == "com.ohos.sceneboard"
            and any(
                n["attributes"]
                .get("id", "")
                .startswith("AppIcon_Image_dev.nativeapi.smoke")
                for n in walk(r)
            )
            for r in t["children"]
        )

    if launcher(tree("phone-home-check")):
        return
    # moveAbilityToBackground can reveal another app from the back stack. Bring
    # our own window forward first, then send the normal Home gesture wholly
    # inside its verified frame. Never inject input into the other application.
    assert "start ability successfully" in guest(
        "aa",
        "start",
        "-a",
        "EntryAbility",
        "-b",
        "dev.nativeapi.smoke",
        "--ps",
        "nativeapiSuite",
        "integrations",
    )
    deadline = time.monotonic() + 10
    while True:
        t = tree("phone-home-own-window")
        if own_roots(t):
            break
        assert time.monotonic() < deadline, (
            "Own window did not return before Home gesture"
        )
        time.sleep(0.1)
    r = main_root(t)
    left, top, right, bottom = rect(r)
    assert bottom == int(instance["hw.lcd.single.height"])
    assert 0 <= left < right <= int(instance["hw.lcd.single.width"])
    x, y = (left + right) // 2, bottom - 2
    end = bottom - (bottom - top) // 3
    owned_point(t, r, x, y, "phone-home-gesture", r)
    assert top <= end < y < bottom
    result = guest("uinput", "-T", "-m", str(x), str(y), str(x), str(end), "200")
    (out / "phone-home-gesture-native.txt").write_text(result)
    deadline = time.monotonic() + 10
    while not launcher(tree("phone-home-after-gesture")):
        assert time.monotonic() < deadline, "Home launcher icon did not become visible"
        time.sleep(0.1)
    print("PASS guarded Home gesture from own application window", flush=True)


def icon(phase, expected):
    t = tree(phase)
    r = main_root(t)
    matches = [n for n in walk(r) if n["attributes"].get("id") == "enhanceAppIcon"]
    assert len(matches) == 1
    owned(t, r, matches[0], phase)
    b = rect(matches[0])
    x, y = (b[0] + b[2]) // 2, (b[1] + b[3]) // 2
    folder = out / f"{phase}-screenshot-{time.time_ns()}"
    folder.mkdir()
    result = run(
        emulator,
        "-instance",
        instance["name"],
        "-screenshot",
        "-screenshotPath",
        folder,
    )
    image = None
    deadline = time.monotonic() + 15
    while time.monotonic() < deadline:
        paths = list(folder.glob("*.png"))
        assert len(paths) <= 1
        if paths:
            try:
                image = Image.open(paths[0]).convert("RGB")
                break
            except OSError:
                pass
        time.sleep(0.1)
    assert image is not None, result
    samples = [
        image.getpixel((x + dx, y + dy)) for dx in [-2, 0, 2] for dy in [-2, 0, 2]
    ]
    assert all(max(abs(c[i] - expected[i]) for i in range(3)) <= 2 for c in samples), (
        phase,
        samples,
    )
    (out / f"{phase}-pixels.json").write_text(
        json.dumps(dict(expected=expected, actual=samples, point=[x, y], bounds=b))
    )
    print("PASS native title-bar icon pixels", phase, samples[0], flush=True)


wait("NATIVEAPI_INTEGRATION_READY")
for badge_index, (command, number) in enumerate(
    [("badge9", 9), ("badge99", 100), ("clear", 0)], 1
):
    click("nativeapi-integration-" + command)
    wait("NATIVEAPI_INTEGRATION_DONE " + command)
    wait("NATIVEAPI_INTEGRATION_BADGE actual=" + str(number) + " ")
    badge_nodes(number)
    if kind == "phone":
        click("nativeapi-integration-preview-badge")
        wait("NATIVEAPI_INTEGRATION_BADGE_PREVIEW", badge_index)
        phone_home()
        nodes = badge_nodes(number)
        if number:
            label = "9" if number == 9 else "99+"
            assert any(n.get("text") == label for n in nodes), (
                "Own phone launcher badge not visible"
            )
        else:
            assert not nodes, "Phone launcher badge was not cleared"
        assert "start ability successfully" in guest(
            "aa",
            "start",
            "-a",
            "EntryAbility",
            "-b",
            "dev.nativeapi.smoke",
            "--ps",
            "nativeapiSuite",
            "integrations",
        )
        time.sleep(0.7)
    print("PASS actual SDK badge", number, flush=True)
if kind == "2in1":
    for command, color in [("icon-a", (255, 0, 255)), ("icon-b", (0, 255, 255))]:
        click("nativeapi-integration-" + command)
        wait("NATIVEAPI_INTEGRATION_DONE " + command)
        icon(command, color)
    for index, button in enumerate(
        ["hide-cpp", "hide-c", "hide-window", "hide-external", "hide-tray"], 1
    ):
        before = tree("before-" + button)
        old = {r["attributes"]["hostWindowId"]: rect(r) for r in own_roots(before)}
        assert len(old) == 2
        click("nativeapi-integration-" + button)
        wait("NATIVEAPI_INTEGRATION_HIDDEN", index)
        hidden = tree("hidden-" + button)
        assert not own_roots(hidden), "An application window remained on screen"
        native = guest("hidumper", "-s", "WindowManagerService", "-a", "-a")
        (out / f"hidden-{button}-native.txt").write_text(native)
        rows = [row for row in window_rows(native) if int(row[2]) == a.pid]
        assert {row[3] for row in rows} == set(old)
        assert all(int(row[7]) < 0 for row in rows)
        if button == "hide-external":
            # SDK hide removes the running Dock item. A normal external launch
            # restores the same Ability and group; it does not call our Show API.
            assert "start ability successfully" in guest(
                "aa",
                "start",
                "-a",
                "EntryAbility",
                "-b",
                "dev.nativeapi.smoke",
                "--ps",
                "nativeapiSuite",
                "integrations",
            )
            wait("NATIVEAPI_INTEGRATION_EXTERNAL_RESTORED")
        elif button == "hide-tray":

            def system_click(identifier):
                t = tree("status-restore-" + identifier)
                matches = [
                    (r, n)
                    for r in t["children"]
                    if r["attributes"].get("bundleName") == "com.ohos.sceneboard"
                    for n in walk(r)
                    if n["attributes"].get("id") == identifier
                ]
                assert len(matches) == 1
                r, n = matches[0]
                left, top, right, bottom = rect(n)
                x, y = (left + right) // 2, (top + bottom) // 2
                assert right > left and bottom > top
                hits = [
                    q
                    for q in t["children"]
                    if rect(q)[0] <= x < rect(q)[2] and rect(q)[1] <= y < rect(q)[3]
                ]
                assert (
                    len(hits) == 1
                    and hits[0]["attributes"]["hostWindowId"]
                    == r["attributes"]["hostWindowId"]
                )
                windows = guest("hidumper", "-s", "WindowManagerService", "-a", "-a")
                wid = r["attributes"]["hostWindowId"]
                rows = [row for row in window_rows(windows) if row[3] == wid]
                assert (
                    len(rows) == 1
                    and int(rows[0][7]) > 0
                    and rows[0][2] in guest("pidof", "com.ohos.sceneboard").split()
                )
                if identifier.startswith("PluginRootComponent_Tray_"):
                    assert any(
                        v["attributes"].get("id") == "GridItemNativeAPI Smoke"
                        for v in walk(r)
                    )
                identity()
                (out / (identifier + "-owner.json")).write_text(
                    json.dumps(
                        dict(
                            window=wid,
                            pid=rows[0][2],
                            point=[x, y],
                            node=n["attributes"],
                        )
                    )
                )
                result = run(
                    emulator, "-instance", instance["name"], "-click", f"{x} {y}"
                )
                assert "Scenario simulation success" in result, result

            system_click("DockStatusBarComponent_arrowbutton")
            token = json.loads(metadata[metadata.index("{") :])["applicationInfo"][
                "accessTokenId"
            ]
            system_click("PluginRootComponent_Tray_" + str(token))
            wait("NATIVEAPI_INTEGRATION_TRAY_RESTORED")
        else:
            wait("NATIVEAPI_INTEGRATION_RESTORED", index)
        after = tree("restored-" + button)
        new = {r["attributes"]["hostWindowId"]: rect(r) for r in own_roots(after)}
        assert new == old, (old, new)
        print(
            "PASS actual whole-Ability hide/restore, "
            "stable window identities and rectangles",
            button,
            flush=True,
        )
else:
    text = logs()
    for command in ["hide", "show", "icon"]:
        assert f"NATIVEAPI_INTEGRATION_PHONE_SDK {command} code=801" in text
    print(
        "PASS independent phone SDK rejects unsupported desktop hide/show/icon",
        flush=True,
    )
click("nativeapi-integration-finish")
wait("NATIVEAPI_INTEGRATION_FINAL_PASS")
print("NATIVEAPI_INTEGRATION_UI_PASS", kind, flush=True)
