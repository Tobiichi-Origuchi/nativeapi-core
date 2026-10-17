#!/usr/bin/env python3
"""Sign allowlisted isolated SDK fixtures with public SDK test keys.

Official ACL debugging flow; never a distribution signature. SDK files are read
only. The generated profile/certificates/HAP stay in ignored test-results.
"""

import argparse
import json
import re
import shutil
import subprocess
import time
import uuid
import zipfile
from pathlib import Path

FIXTURES = {
    "keyboard": (
        "dev.nativeapi.keyboard.monitor",
        "ohos.permission.INPUT_MONITORING",
        "keyboard-acl",
    ),
    "device-info-writer": (
        "dev.nativeapi.deviceinfo.writer",
        "ohos.permission.MANAGE_SETTINGS",
        "device-info-writer",
    ),
}


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("deveco", type=Path)
    parser.add_argument("hap", type=Path)
    parser.add_argument("--fixture", choices=list(FIXTURES), default="keyboard")
    parser.add_argument(
        "--guest-udid",
        required=True,
        help="Isolated emulator UDID for an SDK debug profile",
    )
    parser.add_argument("--apl", choices=["normal", "system_basic"], default="normal")
    parser.add_argument(
        "--system-app",
        action="store_true",
        help="SDK system-app test profile; never normal-app evidence",
    )
    args = parser.parse_args()
    bundle, permission, prefix = FIXTURES[args.fixture]
    if args.fixture == "device-info-writer":
        assert args.system_app and args.apl == "system_basic"
    with zipfile.ZipFile(args.hap) as archive:
        manifest = json.loads(archive.read("module.json"))
    assert manifest["app"]["bundleName"] == bundle
    assert manifest["module"]["requestPermissions"] == [{"name": permission}]
    lib = args.deveco / "sdk/default/openharmony/toolchains/lib"
    java = args.deveco / "jbr/Contents/Home/bin/java"
    out = Path(__file__).resolve().parent / f"test-results/{prefix}-signing"
    out.mkdir(parents=True, exist_ok=True)

    def run(*command):
        result = subprocess.run(
            list(map(str, command)),
            stdin=subprocess.DEVNULL,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
            timeout=60,
        )
        assert result.returncode == 0, result.stdout
        return result.stdout

    # 123456 is the SDK's documented PUBLIC sample key password, not user data.
    certs = []
    for alias in [
        "openharmony application release",
        "openharmony application ca",
        "openharmony application root ca",
    ]:
        text = run(
            java.with_name("keytool"),
            "-exportcert",
            "-rfc",
            "-alias",
            alias,
            "-keystore",
            lib / "OpenHarmony.p12",
            "-storepass",
            "123456",
        )
        certs.append(
            re.search(
                r"-----BEGIN CERTIFICATE-----.*?-----END CERTIFICATE-----", text, re.S
            ).group(0)
            + "\n"
        )
    chain = out / "sdk-application-chain.pem"
    ca = out / "sdk-application-ca.cer"
    root = out / "sdk-root-ca.cer"
    ca.write_text(certs[1])
    root.write_text(certs[2])
    keystore = out / "sdk-test-keys.p12"
    shutil.copyfile(lib / "OpenHarmony.p12", keystore)
    keystore.chmod(0o600)
    tool = [java, "-jar", lib / "hap-sign-tool.jar"]
    log = run(
        *tool,
        "generate-app-cert",
        "-keyAlias",
        "openharmony application release",
        "-signAlg",
        "SHA256withECDSA",
        "-issuer",
        "C=CN,O=OpenHarmony,OU=OpenHarmony Team,CN=OpenHarmony Application CA",
        "-issuerKeyAlias",
        "openharmony application ca",
        "-subject",
        "C=CN,O=OpenHarmony,OU=OpenHarmony Team,CN=OpenHarmony Application Release",
        "-keystoreFile",
        keystore,
        "-subCaCertFile",
        ca,
        "-rootCaCertFile",
        root,
        "-outForm",
        "certChain",
        "-outFile",
        chain,
        "-keyPwd",
        "123456",
        "-keystorePwd",
        "123456",
        "-issuerKeyPwd",
        "123456",
        "-validity",
        "2",
    )
    leaf = re.search(
        r"-----BEGIN CERTIFICATE-----.*?-----END CERTIFICATE-----",
        chain.read_text(),
        re.S,
    ).group(0)
    assert re.fullmatch(r"[A-Fa-f0-9]{64}", args.guest_udid)
    profile = json.loads((lib / "UnsgnedDebugProfileTemplate.json").read_text())
    profile["uuid"] = str(uuid.uuid4())
    profile["validity"] = {
        "not-before": int(time.time()) - 3600,
        "not-after": int(time.time()) + 86400,
    }
    profile["bundle-info"].update(
        {
            "bundle-name": bundle,
            "apl": args.apl,
            "development-certificate": leaf + "\n",
        }
    )
    if args.system_app:
        assert args.apl == "system_basic"
        profile["bundle-info"]["app-feature"] = "hos_system_app"
    profile["debug-info"] = {
        "device-id-type": "udid",
        "device-ids": [args.guest_udid],
    }
    profile["permissions"] = {"restricted-permissions": [permission]}
    profile["acls"] = {"allowed-acls": [permission]}
    profile_path = out / f"{prefix}-profile.json"
    profile_path.write_text(json.dumps(profile, indent=2))
    common = [
        "-mode",
        "localSign",
        "-signAlg",
        "SHA256withECDSA",
        "-keystoreFile",
        keystore,
        "-keyPwd",
        "123456",
        "-keystorePwd",
        "123456",
    ]
    signed_profile = out / f"{prefix}-profile.p7b"
    log += run(
        *tool,
        "sign-profile",
        *common,
        "-keyAlias",
        "openharmony application profile debug",
        "-profileCertFile",
        lib / "OpenHarmonyProfileDebug.pem",
        "-inFile",
        profile_path,
        "-outFile",
        signed_profile,
    )
    signed_hap = out / f"{prefix}.hap"
    log += run(
        *tool,
        "sign-app",
        *common,
        "-keyAlias",
        "openharmony application release",
        "-appCertFile",
        chain,
        "-profileFile",
        signed_profile,
        "-inFile",
        args.hap,
        "-outFile",
        signed_hap,
    )
    log += run(
        *tool,
        "verify-app",
        "-inFile",
        signed_hap,
        "-outCertChain",
        out / "verified-chain.cer",
        "-outProfile",
        out / "verified-profile.p7b",
    )
    (out / "signing.log").write_text(log)
    print(signed_hap)


if __name__ == "__main__":
    main()
