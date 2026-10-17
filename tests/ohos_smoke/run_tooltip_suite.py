#!/usr/bin/env python3
"""Real guest-only mouse hover and SDK popup acceptance; no host input or keys."""

import argparse
import json
import re
import time
from pathlib import Path

from ohos_ui_fixture import Fixture


class Tooltips:
    def __init__(self, fixture):
        self.f = fixture
        self.sequence = 0
        self.scene = 0
        self.state = {}
        self.pointer = None
        self.focus = None

    def command(self, action):
        self.sequence += 1
        marker = f"NATIVEAPI_TOOLTIP_ACTION {self.sequence} {action} "
        result = self.f.guest(
            "aa",
            "start",
            "-a",
            "EntryAbility",
            "-b",
            self.f.bundle,
            "--ps",
            "nativeapiSuite",
            "tooltip",
            "--ps",
            "nativeapiTooltipAction",
            f"{self.sequence}:{action}",
        )
        assert "start ability successfully" in result
        records = self.f.wait(marker)
        line = next(line for line in records.splitlines() if marker in line)
        self.state = json.loads(line.split(marker, 1)[1])
        assert self.state["failed"] == 0
        if action in ["disable", "remove", "detach", "destroy"]:
            assert 0 <= self.state["requestAge"] < 700, (action, self.state)
        return self.state

    def nodes(self, phase):
        tree = self.f.tree(phase)
        return [n for root in tree["children"] for n in self.f.walk(root)]

    def point(self, identifier, phase):
        self.f.identity()
        tree = self.f.tree(phase)
        matches = [
            (root, node)
            for root in tree["children"]
            if root["attributes"].get("bundleName") == self.f.bundle
            for node in self.f.walk(root)
            if node["attributes"].get("id") == identifier
        ]
        assert len(matches) == 1, (identifier, matches)
        root, node = matches[0]
        l, t, r, b = self.f.rect(node)
        assert r > l and b > t
        point = [(l + r) // 2, (t + b) // 2]
        hits = [
            w
            for w in tree["children"]
            if self.f.rect(w)[0] <= point[0] < self.f.rect(w)[2]
            and self.f.rect(w)[1] <= point[1] < self.f.rect(w)[3]
        ]
        assert (
            len(hits) == 1
            and hits[0]["attributes"]["hostWindowId"]
            == root["attributes"]["hostWindowId"]
        )
        row = self.f.windows(phase)[int(root["attributes"]["hostWindowId"])]
        assert row[2] == self.f.pid and int(row[7]) > 0
        self.f.identity()
        (self.f.out / f"{phase}-mouse-owner.json").write_text(
            json.dumps(
                {
                    "guest": self.f.instance["name"],
                    "pid": self.f.pid,
                    "uid": self.f.uid,
                    "window": row[3],
                    "point": point,
                    "node": node["attributes"],
                },
                indent=2,
            )
        )
        return point

    def hover(self, key, phase):
        identifier = f"nativeapi-view-{self.state[key]}" if key in self.state else key
        x, y = self.point(identifier, phase)
        # uinput's trace form uses absolute coordinates; no buttons are pressed.
        self.f.guest(
            "uinput", "-M", "-m", str(x), str(y), str(x), str(y), "400", "--trace"
        )
        return x, y

    def away(self, phase):
        nodes = self.nodes(phase + "-find-title")
        titles = [
            n
            for n in nodes
            if n["attributes"].get("text") == "NativeAPI Tooltip Acceptance"
        ]
        assert len(titles) == 1
        self.hover(titles[0]["attributes"]["id"], phase)

    def tooltip(self, text, phase, present=True):
        if not present:
            time.sleep(1.1)  # Observe after the pending tooltip deadline.
        deadline = time.monotonic() + 6
        while time.monotonic() < deadline:
            nodes = self.nodes(phase)
            found = [n for n in nodes if n["attributes"].get("text") == text]
            all_tips = [
                n["attributes"].get("text")
                for n in nodes
                if n["attributes"]
                .get("text", "")
                .startswith(
                    (
                        "NativeAPI tip ",
                        "NativeAPI nested tip",
                        "NativeAPI field tip",
                        "NativeAPI multiline tip",
                    )
                )
            ]
            if bool(found) == present and (present or not all_tips):
                if present:
                    assert all_tips == [text], all_tips
                if present:
                    assert len(found) == 1
                    l, t, r, b = self.f.rect(found[0])
                    assert r > l and b > t
                rows = self.f.windows(phase)
                window_log = (self.f.out / f"{phase}-windows.txt").read_text()
                focus = re.search(
                    r"^Focus window:\s*(\d+)\s*$", window_log, re.MULTILINE
                )
                assert focus and int(focus[1]) in rows
                if self.focus is None:
                    self.focus = int(focus[1])
                assert int(focus[1]) == self.focus, (
                    "Tooltip acquired or changed native window focus"
                )
                self.f.screenshot(phase)
                self.scene += 1
                print("PASS", phase, "visible" if present else "absent", flush=True)
                return
            time.sleep(0.15)
        raise AssertionError((phase, text, present))


def main():
    p = argparse.ArgumentParser()
    p.add_argument("deveco", type=Path)
    p.add_argument("--device", default="127.0.0.1:5555")
    a = p.parse_args()
    f = Fixture(a.deveco, a.device, "tooltip", "TOOLTIP")
    assert f.instance["name"] in ["NativeAPI_Phone", "NativeAPI_Desktop"]
    log = f.wait("NATIVEAPI_TOOLTIP_READY")
    line = next(line for line in log.splitlines() if "NATIVEAPI_TOOLTIP_READY " in line)
    test = Tooltips(f)
    test.state = json.loads(line.split("NATIVEAPI_TOOLTIP_READY ", 1)[1])
    assert test.state["failed"] == 0
    test.hover("primary", "primary-hover")
    test.tooltip("NativeAPI tip A 中文 🙂", "primary-visible")
    test.command("update")
    test.tooltip("NativeAPI tip A updated 更新 🙂", "updated-while-hovered")
    test.command("clear")
    test.tooltip("NativeAPI tip A updated 更新 🙂", "clear-visible", False)
    test.command("empty")
    test.tooltip("NativeAPI tip A 中文 🙂", "empty-absent", False)
    test.command("restore")
    test.tooltip("NativeAPI tip A 中文 🙂", "restore-while-hovered")
    # A nonmodal popup must neither acquire native window focus nor eat this click.
    f.click(f"nativeapi-view-{test.state['primary']}", "click-primary-through-tip")
    test.away("leave-primary")
    test.tooltip("NativeAPI tip A 中文 🙂", "leave-dismisses", False)
    test.hover("secondary", "c-hover")
    test.tooltip("NativeAPI tip B via C", "c-visible")
    f.click(f"nativeapi-view-{test.state['secondary']}", "click-secondary-through-tip")
    test.command("click-check")
    test.away("before-disable")
    test.hover("primary", "disabled-pending-hover")
    test.command("disable")
    test.tooltip("NativeAPI tip A 中文 🙂", "disabled-pending-cancelled", False)
    test.command("enable")
    test.away("after-enable")
    test.hover("primary", "enabled-hover")
    test.tooltip("NativeAPI tip A 中文 🙂", "enabled-visible")
    test.command("hide")
    test.tooltip("NativeAPI tip A 中文 🙂", "hidden-dismisses", False)
    test.command("show")
    test.away("after-show")
    test.hover("nested", "nested-no-listeners-hover")
    test.tooltip("NativeAPI nested tip", "nested-visible")
    test.command("disable-parent")
    test.tooltip("NativeAPI nested tip", "disabled-parent-dismisses", False)
    test.command("enable-parent")
    test.away("after-parent-enable")
    test.hover("nested", "nested-second-hover")
    test.tooltip("NativeAPI nested tip", "nested-restored")
    test.command("hide-parent")
    test.tooltip("NativeAPI nested tip", "hidden-parent-dismisses", False)
    test.command("show-parent")
    test.away("before-remove")
    test.hover("primary", "remove-pending-hover")
    test.command("remove")
    test.tooltip("NativeAPI tip A 中文 🙂", "removed-pending-cancelled", False)
    test.command("readd")
    test.away("after-readd")
    test.hover("primary", "readded-hover")
    test.tooltip("NativeAPI tip A 中文 🙂", "readded-visible")
    test.away("before-field")
    test.hover("field", "field-hover")
    test.tooltip("NativeAPI field tip", "field-visible")
    old_node = test.state["fieldNode"]
    old_view = test.state["field"]
    test.command("multiline")
    assert test.state["fieldNode"] != old_node and test.state["field"] == old_view
    test.tooltip("NativeAPI multiline tip", "replacement-shows-new-tip")
    test.away("after-field-replace")
    test.hover("field", "multiline-hover")
    test.tooltip("NativeAPI multiline tip", "multiline-visible")
    test.away("before-detach")
    test.hover("primary", "detach-pending-hover")
    test.command("detach")
    test.tooltip("NativeAPI tip A 中文 🙂", "detached-pending-cancelled", False)
    test.command("reattach")
    test.away("after-reattach")
    test.hover("primary", "reattached-hover")
    test.tooltip("NativeAPI tip A 中文 🙂", "reattached-visible")
    test.command("replace-host")
    test.tooltip("NativeAPI tip A 中文 🙂", "replaced-host-dismisses", False)
    test.away("after-host-replace")
    test.hover("primary", "replacement-host-hover")
    test.tooltip("NativeAPI tip A 中文 🙂", "replacement-host-visible")
    test.away("before-destroy")
    test.hover("primary", "destroy-pending-hover")
    test.command("destroy")
    test.tooltip("NativeAPI tip A 中文 🙂", "destroyed-pending-cancelled", False)
    test.command("query")
    assert (
        test.state["passed"] == 8
        and test.state["failed"] == 0
        and test.state["clicks"] == 2
    )
    assert test.state["primary"] == 0 and test.scene == 24, test.scene
    summary = {
        "guest": f.instance["name"],
        "pid": f.pid,
        "uid": f.uid,
        "native": test.state,
        "scenes": test.scene,
        "evidence": str(f.out),
    }
    (f.out / "summary.json").write_text(json.dumps(summary, indent=2))
    print("PASS", json.dumps(summary), flush=True)


if __name__ == "__main__":
    main()
