#!/usr/bin/env python3
"""Build the normal device reader or the isolated Settings writer on a named guest."""

import argparse
import hashlib
import json
import os
import re
import shutil
import subprocess
import zipfile
from pathlib import Path


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("deveco", type=Path)
    parser.add_argument("--device", default="127.0.0.1:5555")
    parser.add_argument("--profile", choices=["ordinary", "writer"], required=True)
    args = parser.parse_args()
    assert re.fullmatch(r"127\.0\.0\.1:\d+", args.device)
    wd = Path(__file__).resolve().parent
    hdc = args.deveco / "sdk/default/openharmony/toolchains/hdc"
    out = wd / "test-results"
    out.mkdir(exist_ok=True)

    def run(*command, **kwargs):
        return subprocess.check_output(
            list(map(str, command)),
            stdin=subprocess.DEVNULL,
            text=True,
            stderr=subprocess.STDOUT,
            timeout=180,
            **kwargs,
        )

    def guest(*command):
        return run(hdc, "-t", args.device, "shell", *command)

    assert guest("param", "get", "const.product.model").strip() == "emulator"
    kind = guest("param", "get", "const.product.devicetype").strip()
    expected = {"phone": "NativeAPI_Phone", "2in1": "NativeAPI_Desktop"}[kind]
    instances = json.loads(
        run(args.deveco / "tools/emulator/Emulator", "-list", "-details")
    )
    live = [instance for instance in instances if instance["isRunning"] == "true"]
    assert len(live) == 1 and live[0]["name"] == expected
    env = dict(
        os.environ,
        DEVECO_SDK_HOME=str(args.deveco / "sdk"),
        NATIVEAPI_OHOS_DESKTOP="1" if kind == "2in1" else "0",
        NATIVEAPI_OHOS_NOTIFICATION_DENIAL="0",
        NATIVEAPI_OHOS_DEVICEINFO_WRITER="0" if args.profile == "ordinary" else "1",
        NATIVEAPI_OHOS_KEYBOARD_ACL="0",
    )
    log = run(
        args.deveco / "tools/node/bin/node",
        args.deveco / "tools/hvigor/bin/hvigorw.js",
        "--mode",
        "module",
        "-p",
        "product=default",
        "assembleHap",
        "--no-daemon",
        cwd=wd,
        env=env,
    )
    (out / f"device-info-{kind}-{args.profile}-build.log").write_text(log)
    hap = wd / "entry/build/default/outputs/default/entry-default-unsigned.hap"
    with zipfile.ZipFile(hap) as archive:
        manifest = json.loads(archive.read("module.json"))
    bundle = (
        "dev.nativeapi.smoke"
        if args.profile == "ordinary"
        else "dev.nativeapi.deviceinfo.writer"
    )
    assert manifest["app"]["bundleName"] == bundle
    devices = manifest["module"]["deviceTypes"]
    assert devices == ["2in1"] if kind == "2in1" else "phone" in devices
    if args.profile == "ordinary":
        saved_hap = out / f"device-info-{kind}-ordinary.hap"
        shutil.copyfile(hap, saved_hap)
        hap = saved_hap
        assert "ohos.permission.MANAGE_SETTINGS" not in [
            p["name"] for p in manifest["module"]["requestPermissions"]
        ]
    else:
        assert manifest["module"]["requestPermissions"] == [
            {"name": "ohos.permission.MANAGE_SETTINGS"}
        ]
        udid = re.search(r"[a-fA-F0-9]{64}", guest("bm", "get", "-u")).group(0)
        options = [
            "--guest-udid",
            udid,
            "--fixture",
            "device-info-writer",
            "--apl",
            "system_basic",
            "--system-app",
        ]
        print(
            run("python3", wd / "sign_debug_fixture.py", args.deveco, hap, *options),
            flush=True,
        )
        hap = out / "device-info-writer-signing/device-info-writer.hap"
        # Preserve the writer's saved original name across updates/restarts.
        # A previous interrupted test can restore through its own SDK preferences.
    guest("aa", "force-stop", bundle)
    installed = run(hdc, "-t", args.device, "install", hap)
    assert "install bundle successfully" in installed, installed
    if args.profile == "ordinary":
        (out / "build-device-type.txt").write_text(env["NATIVEAPI_OHOS_DESKTOP"] + "\n")
    metadata = guest("bm", "dump", "-n", bundle)
    (out / f"device-info-{kind}-{args.profile}-bundle.json").write_text(metadata)
    token = guest("atm", "dump", "-t", "-b", bundle)
    (out / f"device-info-{kind}-{args.profile}-token.json").write_text(token)
    report = dict(
        guest=expected,
        profile=args.profile,
        bundle=bundle,
        hap=str(hap),
        sha256=hashlib.sha256(hap.read_bytes()).hexdigest(),
    )
    (out / f"device-info-{kind}-{args.profile}-build.json").write_text(
        json.dumps(report, indent=2)
    )
    print(json.dumps(report), flush=True)


if __name__ == "__main__":
    main()
