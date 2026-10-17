#!/usr/bin/env python3
"""Exercise only the fixture's owned status item in a named disposable guest."""

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
p.add_argument("--phase", choices=["base", "menu", "all"], default="all")
a = p.parse_args()
assert re.fullmatch(r"127\.0\.0\.1:\d+", a.device)
suffix = ".exe" if os.name == "nt" else ""
hdc = a.deveco / f"sdk/default/openharmony/toolchains/hdc{suffix}"
emulator = a.deveco / f"tools/emulator/Emulator{suffix}"
out = Path(__file__).resolve().parent / "test-results/tray-ui" / str(a.pid)
out.mkdir(parents=True, exist_ok=True)


def run(*cmd):
    return subprocess.check_output(
        list(map(str, cmd)),
        stdin=subprocess.DEVNULL,
        text=True,
        encoding="utf-8",
        stderr=subprocess.STDOUT,
        timeout=30,
    )


def guest(*cmd):
    return run(hdc, "-t", a.device, "shell", *cmd)


assert guest("param", "get", "const.product.model").strip() == "emulator"
assert guest("param", "get", "const.product.devicetype").strip() == "2in1"
instances = [
    i
    for i in json.loads(run(emulator, "-list", "-details"))
    if i["isRunning"] == "true"
]
assert len(instances) == 1 and instances[0]["name"] == "NativeAPI_Desktop"
instance = instances[0]
raw = guest("bm", "dump", "-n", "dev.nativeapi.smoke")
app = json.loads(raw[raw.index("{") :])["applicationInfo"]
uid = app["uid"]
token = app["accessTokenId"]


def identity():
    assert guest("pidof", "dev.nativeapi.smoke").strip() == str(a.pid)
    assert (
        guest("cat", f"/proc/{a.pid}/cmdline").split("\0")[0] == "dev.nativeapi.smoke"
    )
    status = guest("cat", f"/proc/{a.pid}/status")
    m = re.search(r"^Uid:\s+(\d+)\s+(\d+)\s+(\d+)\s+(\d+)", status, re.M)
    assert m and all(int(x) == uid for x in m.groups())


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
    remote = "/data/local/tmp/nativeapi-tray-ui.json"
    assert "DumpLayout saved" in guest("uitest", "dumpLayout", "-p", remote)
    path = out / f"{phase}-{time.time_ns()}.json"
    run(hdc, "-t", a.device, "file", "recv", remote, path)
    return json.loads(path.read_text())


def find(t, identifier=None, text=None):
    return [
        (r, n)
        for r in t["children"]
        for n in walk(r)
        if (identifier is not None and n["attributes"].get("id") == identifier)
        or (text is not None and n["attributes"].get("text") == text)
    ]


def owned(t, r, n, phase):
    identity()
    b = rect(n)
    assert b[2] > b[0] and b[3] > b[1]
    x, y = (b[0] + b[2]) // 2, (b[1] + b[3]) // 2
    hits = [
        v
        for v in t["children"]
        if rect(v)[0] <= x < rect(v)[2] and rect(v)[1] <= y < rect(v)[3]
    ]
    # SDK hover tips use a fullscreen SceneBoard Popup whose actual content is
    # above the icon. Admit only this fixture-token tooltip, never an arbitrary overlay.
    for overlay in list(hits):
        if overlay["attributes"]["hostWindowId"] == r["attributes"]["hostWindowId"]:
            continue
        tips = [
            v
            for v in walk(overlay)
            if v["attributes"].get("id") == f"StatusBarPluginForPC_Text_{token}"
        ]
        assert (
            overlay["attributes"].get("bundleName") == "com.ohos.sceneboard"
            and len(tips) == 1
        )
        assert tips[0]["attributes"].get("text") in [
            "NativeAPI tray initial",
            "NativeAPI worker tray",
            "NativeAPI tray updated",
            "NativeAPI tray burst final",
        ]
        for v in walk(overlay):
            assert v["attributes"].get("clickable") != "true"
        q = rect(tips[0])
        assert not (q[0] <= x < q[2] and q[1] <= y < q[3])
        hits.remove(overlay)
    assert (
        len(hits) == 1
        and hits[0]["attributes"]["hostWindowId"] == r["attributes"]["hostWindowId"]
    )
    bundle = r["attributes"].get("bundleName")
    assert bundle in ["dev.nativeapi.smoke", "com.ohos.sceneboard"]
    wid = r["attributes"]["hostWindowId"]
    windows = guest("hidumper", "-s", "WindowManagerService", "-a", "-a")
    rows = [
        l.split()
        for l in windows.splitlines()
        if re.match(r"^\S+\s+\d+\s+\d+\s+" + re.escape(wid) + r"\s+", l)
    ]
    assert len(rows) == 1 and int(rows[0][7]) > 0
    assert rows[0][2] in guest("pidof", bundle).split()
    if bundle == "dev.nativeapi.smoke":
        assert int(rows[0][2]) == a.pid
    else:
        # System targets are restricted to this test's status icon/menu, or its overflow button.
        identifiers = [v["attributes"].get("id") for v in walk(r)]
        texts = [v["attributes"].get("text") for v in walk(r)]
        assert (
            n["attributes"].get("id") == "DockStatusBarComponent_arrowbutton"
            or (
                f"PluginRootComponent_Tray_{token}" in identifiers
                and "GridItemNativeAPI Smoke" in identifiers
            )
            or any(
                s in texts
                for s in [
                    "NativeAPI Tray Action",
                    "NativeAPI Tray Updated Action",
                    "NativeAPI Tray Nested",
                ]
            )
        )
    (out / f"{phase}-point.json").write_text(
        json.dumps(
            dict(
                bundle=bundle,
                pid=rows[0][2],
                window=wid,
                point=[x, y],
                node=n["attributes"],
            )
        )
    )
    (out / f"{phase}-windows.txt").write_text(windows)
    return x, y


def click(identifier=None, text=None, mouse=False, right=False):
    phase = identifier or text
    t = tree(phase)
    matches = find(t, identifier, text)
    assert len(matches) == 1, (phase, len(matches))
    r, n = matches[0]
    assert n["attributes"].get("enabled") != "false"
    x, y = owned(t, r, n, phase)
    if mouse:
        guest("uinput", "-M", "-m", x, y)
        # Hover can create a native tooltip/menu. Re-read ownership before every press.
        time.sleep(0.2)
        fresh = tree(phase + "-moved")
        matches = find(fresh, identifier, text)
        assert len(matches) == 1
        xx, yy = owned(fresh, *matches[0], phase + "-press")
        assert (x, y) == (xx, yy)
        guest("uinput", "-M", "-m", x, y, "-c", 1 if right else 0)
    else:
        result = run(emulator, "-instance", instance["name"], "-click", f"{x} {y}")
        assert "Scenario simulation success" in result, result
    time.sleep(0.3)


def logs():
    text = guest("hilog", "-x", "-P", a.pid)
    (out / "native.log").write_text(text)
    assert not re.search(r"\bFAIL\b|NATIVEAPI_TRAY_(ERROR|TIMEOUT)", text), text[-8000:]
    return text


def wait(marker):
    deadline = time.monotonic() + 30
    while time.monotonic() < deadline:
        identity()
        text = logs()
        if marker in text:
            return text
        time.sleep(0.25)
    raise AssertionError(f"No marker {marker}")


def overflow(require_icon=True):
    t = tree("overflow-check")
    if not find(t, identifier=f"PluginRootComponent_Tray_{token}"):
        click(identifier="DockStatusBarComponent_arrowbutton")
    t = tree("overflow-open")
    matches = find(t, identifier=f"PluginRootComponent_Tray_{token}")
    assert len(matches) == 1 or not require_icon
    return (t, matches[0]) if matches else (t, None)


def pixels(phase, expected):
    t, (r, n) = overflow()
    owned(t, r, n, phase)
    images = [v for v in walk(n) if v["attributes"].get("id") == str(token)]
    assert len(images) == 1
    b = rect(images[0])
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
    deadline = time.monotonic() + 15
    picture = None
    while time.monotonic() < deadline:
        paths = list(folder.glob("*.png"))
        assert len(paths) <= 1
        if paths:
            try:
                picture = Image.open(paths[0]).convert("RGB")
                break
            except OSError:
                pass
        time.sleep(0.1)
    assert picture is not None, result
    assert picture.size == (3120, 2080)
    actual = [
        picture.getpixel((x + dx, y + dy)) for dx in [-2, 0, 2] for dy in [-2, 0, 2]
    ]
    if expected is None:
        assert all(max(v) - min(v) <= 2 for v in actual), actual
    else:
        assert all(
            max(abs(v[i] - expected[i]) for i in range(3)) <= 2 for v in actual
        ), actual
    (out / f"{phase}-pixels.json").write_text(
        json.dumps(dict(expected=expected, actual=actual, point=[x, y], imageBounds=b))
    )
    print("PASS actual native tray pixels", phase, actual[0], flush=True)


def button(command, done=None):
    click(identifier="nativeapi-tray-" + command)
    if done:
        wait("NATIVEAPI_TRAY_UI_DONE " + done)


wait("NATIVEAPI_TRAY_READY")
if a.phase in ["base", "all"]:
    pixels("magenta", (255, 0, 255))
    click(identifier=f"PluginRootComponent_Tray_{token}")
    wait("C ABI observes actual tray click")
    wait("C++ receives actual SDK left click on UI thread")
    button("update", "update")
    pixels("cyan", (0, 255, 255))
    button("template", "template")
    pixels("template", None)
    button("restore", "restore")
    pixels("restored", (0, 255, 255))
    button("tooltip", "tooltip")
    button("bad-tooltip", "bad-tooltip")
    button("burst", "burst")
    pixels("burst", (0, 255, 255))
    print(
        "PASS real SDK replacement, template, restoration and hover update/refusal",
        flush=True,
    )
if a.phase in ["menu", "all"]:
    overflow()
    click(identifier=f"PluginRootComponent_Tray_{token}", mouse=True, right=True)
    t = tree("menu-open")
    print(
        "MENU ROOTS",
        [
            (r["attributes"].get("bundleName"), r["attributes"].get("hostWindowId"))
            for r in t["children"]
        ],
        flush=True,
    )
    matches = find(t, text="NativeAPI Tray Action")
    assert len(matches) == 1, "SDK menu did not open"
    click(text="NativeAPI Tray Action")
    wait("real status menu click reaches native MenuItem")
    button("menu", "rename-menu")
    overflow()
    click(identifier=f"PluginRootComponent_Tray_{token}", mouse=True, right=True)
    t = tree("menu-updated")
    assert len(find(t, text="NativeAPI Tray Updated Action")) == 1 and not find(
        t, text="NativeAPI Tray Action"
    )
    click(text="NativeAPI Tray Updated Action")
    print("PASS actual SDK menu and live label update", flush=True)
if a.phase == "all":
    button("hide", "hide")
    overflow(False)
    t = tree("hidden")
    assert not find(t, identifier=f"PluginRootComponent_Tray_{token}")
    button("show", "show")
    pixels("reshown", (0, 255, 255))
    button("dispose", "dispose")
    wait("NATIVEAPI_TRAY_FINAL_PASS")
    overflow(False)
    t = tree("disposed")
    assert not find(t, identifier=f"PluginRootComponent_Tray_{token}")
    print(
        "PASS C ABI hide/show and last-owner removal from real system tree", flush=True
    )
print("NATIVEAPI_TRAY_UI_PASS", a.phase, flush=True)
