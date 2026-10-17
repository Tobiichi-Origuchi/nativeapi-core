"""Guarded startup Settings UI for the isolated NativeAPI fixture only."""

import json
import re
import time

BUNDLE = "com.huawei.hmos.settings"
LABEL = "NativeAPI Smoke"


class StartupSettings:
    def __init__(self, fixture):
        self.f = fixture
        metadata = self.f.guest("bm", "dump", "-n", BUNDLE)
        data = json.loads(metadata[metadata.index("{") :])
        self.uid = data["applicationInfo"]["uid"]
        self.ability = data["hapModuleInfos"][0]["mainElementName"]
        (self.f.out / "settings-metadata.json").write_text(metadata)

    def open(self):
        assert "start ability successfully" in self.f.guest(
            "aa", "start", "-a", self.ability, "-b", BUNDLE
        )
        time.sleep(0.5)

    def snapshot(self, phase):
        tree = self.f.tree("settings-" + phase)
        roots = [
            root
            for root in tree["children"]
            if root["attributes"].get("bundleName") == BUNDLE
        ]
        assert roots, "Expected the actual system Settings application"
        return tree, roots

    def click(self, phase, *, identifier=None, text=None, row=None, target=None):
        assert sum(x is not None for x in [identifier, text, row]) == 1
        if row is not None:
            assert row == LABEL and target in ["Toggle", "移除"]
        tree, roots = self.snapshot(phase)
        if row is not None:
            assert (
                sum(
                    n["attributes"].get("text") == row
                    for root in roots
                    for n in self.f.walk(root)
                )
                == 1
            ), "Ambiguous fixture label in Settings"
        matches = []
        for root in roots:
            if row is None:
                matches.extend(
                    (root, n)
                    for n in self.f.walk(root)
                    if n["attributes"].get("id" if identifier is not None else "text")
                    == (identifier or text)
                )
            else:
                candidates = []
                for n in self.f.walk(root):
                    nodes = list(self.f.walk(n))
                    if sum(x["attributes"].get("text") == row for x in nodes) != 1:
                        continue
                    controls = [
                        x
                        for x in nodes
                        if x["attributes"].get("type" if target == "Toggle" else "text")
                        == target
                    ]
                    if len(controls) == 1:
                        candidates.append((len(nodes), controls[0]))
                if candidates:
                    matches.append((root, min(candidates, key=lambda x: x[0])[1]))
        assert len(matches) == 1, (phase, len(matches))
        root, node = matches[0]
        assert node["attributes"].get("enabled") == "true"
        left, top, right, bottom = self.f.rect(node)
        assert right > left and bottom > top
        x, y = (left + right) // 2, (top + bottom) // 2
        hits = [
            r
            for r in tree["children"]
            if self.f.rect(r)[0] <= x < self.f.rect(r)[2]
            and self.f.rect(r)[1] <= y < self.f.rect(r)[3]
        ]
        wid = root["attributes"]["hostWindowId"]
        assert len(hits) == 1 and hits[0]["attributes"]["hostWindowId"] == wid
        pid = self.f.guest("pidof", BUNDLE).strip()
        assert re.fullmatch(r"\d+", pid)
        assert self.f.guest("cat", f"/proc/{pid}/cmdline").split("\0")[0] == BUNDLE
        status = self.f.guest("cat", f"/proc/{pid}/status")
        uid = re.search(r"^Uid:\s+(\d+)\s+(\d+)\s+(\d+)\s+(\d+)", status, re.M)
        assert uid and all(int(v) == self.uid for v in uid.groups())
        instances = [
            r
            for r in json.loads(self.f.run(self.f.emulator, "-list", "-details"))
            if r["isRunning"] == "true"
        ]
        assert len(instances) == 1 and instances[0]["name"] == self.f.instance["name"]
        assert instances[0]["name"] in ["NativeAPI_Desktop", "NativeAPI_Phone"]
        windows = self.f.guest("hidumper", "-s", "WindowManagerService", "-a", "-a")
        rows = [
            r
            for r in windows.splitlines()
            if re.match(r"^\S+\s+\d+\s+" + pid + r"\s+" + wid + r"\s+", r)
        ]
        focus = re.search(r"^Focus window:\s*(\d+)\s*$", windows, re.M)
        assert len(rows) == 1 and focus and focus[1] == wid
        self.f.identity()
        (self.f.out / (phase + "-settings-owner.json")).write_text(
            json.dumps(
                dict(
                    guest=instances[0]["name"],
                    bundle=BUNDLE,
                    pid=pid,
                    uid=self.uid,
                    window=wid,
                    point=[x, y],
                    node=node["attributes"],
                ),
                indent=2,
            )
        )
        assert "Scenario simulation success" in self.f.run(
            self.f.emulator, "-instance", instances[0]["name"], "-click", f"{x} {y}"
        )
        time.sleep(0.5)
        self.snapshot(phase + "-after")

    def desktop(self, enabled):
        assert self.f.kind == "2in1"
        self.open()
        _, roots = self.snapshot("navigation")
        ids = {n["attributes"].get("id") for r in roots for n in self.f.walk(r)}
        if "Setting.pc_app_setup_settings.title_id" not in ids:
            if (
                "Setting.Application.ApplicationTab.AppSetupGroup.AppSetup.title"
                not in ids
            ):
                self.click("navigate-applications", identifier="application_settings")
            self.click(
                "navigate-startup",
                identifier="Setting.Application.ApplicationTab.AppSetupGroup.AppSetup.title",
            )
        _, roots = self.snapshot("startup-current")
        exists = any(
            n["attributes"].get("text") == LABEL for r in roots for n in self.f.walk(r)
        )
        if exists == enabled:
            return
        if enabled:
            self.click("add-app", text="添加应用")
            self.click("select-own-app", row=LABEL, target="Toggle")
            self.click("confirm-own-app", text="完成")
        else:
            self.click("remove-own-app", row=LABEL, target="移除")
        _, roots = self.snapshot("startup-verified")
        assert (
            any(
                n["attributes"].get("text") == LABEL
                for r in roots
                for n in self.f.walk(r)
            )
            == enabled
        )
