#!/usr/bin/env python3
"""Real system caption closes, shared C++/C votes and SDK-owned destruction."""

import argparse
import json
import re
import time
from pathlib import Path

from ohos_ui_fixture import Fixture


def main():
    p = argparse.ArgumentParser()
    p.add_argument("deveco", type=Path)
    p.add_argument("--device", default="127.0.0.1:5555")
    a = p.parse_args()
    f = Fixture(a.deveco, a.device, "native-close", "NATIVE_CLOSE")

    def faults():
        return sorted(
            name
            for name in f.guest(
                "ls", "-1", "/data/log/faultlog/faultlogger"
            ).splitlines()
            if re.match(r"(?:cppcrash|jscrash|appfreeze)-dev\.nativeapi\.smoke-", name)
        )

    original_faults = faults()
    (f.out / "faults-before.json").write_text(json.dumps(original_faults))
    f.wait("NATIVEAPI_NATIVE_CLOSE_READY")
    clicks = 0
    commands = 0

    def command(name):
        nonlocal commands
        commands += 1
        nonce = f"close-{time.time_ns()}"
        f.guest(
            "aa",
            "start",
            "-a",
            "EntryAbility",
            "-b",
            "dev.nativeapi.smoke",
            "--ps",
            "nativeapiSuite",
            "native-close",
            "--ps",
            "nativeapiNativeCloseCommand",
            name,
            "--ps",
            "nativeapiNativeCloseRequest",
            nonce,
        )
        marker = "NATIVEAPI_NATIVE_CLOSE_REPLY " + nonce + " "
        logs = f.wait(marker)
        assert "nativeapi-window: off failed" not in logs
        answer = json.loads(
            next(line.split(marker)[1] for line in logs.splitlines() if marker in line)
        )
        assert answer["failed"] == 0 and answer["managerRequests"] == 0, answer
        return answer

    def until(check, label):
        deadline = time.monotonic() + 12
        while time.monotonic() < deadline:
            result = command("state")
            if check(result):
                return result
            time.sleep(0.15)
        raise AssertionError((label, result))

    def system_button(window_id, phase, click=True):
        nonlocal clicks
        tree = f.tree(phase)
        roots = [
            r
            for r in tree["children"]
            if r["attributes"].get("bundleName") == "dev.nativeapi.smoke"
            and int(r["attributes"]["hostWindowId"]) == window_id
        ]
        assert len(roots) == 1, (window_id, roots)
        nodes = [
            n
            for n in f.walk(roots[0])
            if n["attributes"].get("id") == "EnhanceCloseBtn"
            and n["attributes"].get("visible") == "true"
            and float(n["attributes"].get("opacity", "1")) > 0
            and f.rect(n)[2] > f.rect(n)[0]
            and f.rect(n)[3] > f.rect(n)[1]
        ]
        if not click:
            return len(nodes)
        assert len(nodes) == 1, (phase, nodes)
        left, top, right, bottom = f.rect(nodes[0])
        x, y = (left + right) // 2, (top + bottom) // 2
        hits = [
            root
            for root in tree["children"]
            if f.rect(root)[0] <= x < f.rect(root)[2]
            and f.rect(root)[1] <= y < f.rect(root)[3]
        ]
        assert len(hits) == 1 and hits[0]["attributes"]["hostWindowId"] == str(
            window_id
        )
        rows = f.windows(phase)
        assert rows[window_id][2] == f.pid and int(rows[window_id][7]) > 0
        f.identity()
        (f.out / f"{phase}-owner.json").write_text(
            json.dumps(
                dict(
                    pid=f.pid,
                    uid=f.uid,
                    window=window_id,
                    point=[x, y],
                    node=nodes[0]["attributes"],
                )
            )
        )
        answer = f.run(
            f.emulator, "-instance", f.instance["name"], "-click", f"{x} {y}"
        )
        assert "Scenario simulation success" in answer
        clicks += 1

    def request(native, identity, label):
        before = command("state")
        if native:
            system_button(identity, label)
        else:
            command("programmatic")
        return until(lambda s: s["requests"] == before["requests"] + 1, label)

    main_state = command("main-programmatic")
    main_id = main_state["systemId"]
    for mode in ["cancel", "c-cancel", "defer", "throw"]:
        if f.kind != "2in1":
            break
        command(mode)
        r = request(True, main_id, "main-" + mode)
        assert r["alive"]
        if mode == "defer":
            count = r["requests"]
            system_button(main_id, "main-repeat")
            assert command("state")["requests"] == count
            w = command("state")["workers"]
            command("approve-a")
            until(lambda s, w=w: s["workers"] > w, "first-worker")
            assert command("state")["pending"]
            w = command("state")["workers"]
            command("reject-c")
            until(lambda s, w=w: s["workers"] > w and not s["pending"], "worker-veto")
        r = command("verify")
        assert r["alive"] and not r["pending"]
        f.screenshot("main-" + mode)
        print("PASS actual main close", mode, flush=True)

    for mode in [
        "cancel",
        "c-cancel",
        "defer-cancel",
        "defer-accept",
        "defer-unlisten",
        "destroy-pending",
        "accept",
        "unlisten",
    ]:
        created = command("create")
        child_id = created["systemId"]
        assert created["alive"] and created["requests"] == 0
        time.sleep(0.3)
        count = system_button(child_id, mode + "-capability", False)
        native = count == 1
        assert native or f.kind == "phone", (mode, count)
        command(
            "defer"
            if mode.startswith("defer") or mode == "destroy-pending"
            else "accept"
            if mode == "unlisten"
            else mode
        )
        if mode == "unlisten":
            command("unlisten")
            if native:
                system_button(child_id, mode)
            else:
                command(
                    "programmatic"
                )  # No observers, so the SDK destroys immediately.
        else:
            r = request(native, child_id, mode)
            if mode.startswith("defer") or mode == "destroy-pending":
                command("coalesce")
                assert command("state")["pending"]
                if mode == "defer-unlisten":
                    command("detach-all")
                if native:
                    system_button(child_id, mode + "-repeat")
                if mode == "destroy-pending":
                    command("destroy")
                    until(lambda s: not s["alive"], mode)
                    w = command("state")["workers"]
                    command("late")
                    until(lambda s, w=w: s["workers"] > w, "late-votes")
                else:
                    w = command("state")["workers"]
                    command("approve-a")
                    until(lambda s, w=w: s["workers"] > w, "first-approval")
                    assert command("state")["pending"] and command("state")["alive"]
                    w = command("state")["workers"]
                    command("reject-c" if mode == "defer-cancel" else "approve-c")
                    until(
                        lambda s, w=w: s["workers"] > w and not s["pending"],
                        "last-vote",
                    )
        closes = mode in [
            "defer-accept",
            "defer-unlisten",
            "destroy-pending",
            "accept",
            "unlisten",
        ]
        r = until(
            lambda s, closes=closes: s["alive"] != closes and not s["pending"],
            mode + "-settled",
        )
        command("verify")
        f.screenshot(mode)
        if not closes:
            command("destroy")
            until(lambda s: not s["alive"], mode + "-cleanup")
            command("verify")
        assert child_id not in f.windows(mode + "-removed")
        if mode == "defer-unlisten":
            command("attach-main")
        print(
            "PASS real child close",
            mode,
            "system" if native else "programmatic; no system caption",
            flush=True,
        )
    r = command("main")
    assert system_button(main_id, "main-final-capability", False) == (
        1 if f.kind == "2in1" else 0
    )
    f.screenshot("complete")
    main_accepted = False
    if f.kind == "2in1":
        command("accept")
        before = command("state")
        system_button(main_id, "main-final-accept")
        deadline = time.monotonic() + 15
        while time.monotonic() < deadline:
            if not f.guest("pidof", "dev.nativeapi.smoke").strip():
                break
            time.sleep(0.1)
        else:
            raise AssertionError("SDK main close did not terminate the owning Ability")
        logs = f.guest("hilog", "-x", "-P", f.pid)
        (f.out / "terminated-native.log").write_text(logs)
        assert not re.search(r"\bFAIL\b|NATIVEAPI_NATIVE_CLOSE_ERROR", logs)
        assert "nativeapi-window: off failed" not in logs
        assert f"NATIVEAPI_NATIVE_CLOSE_EVENT {before['requests'] + 1} accept" in logs
        rows = f.guest("hidumper", "-s", "WindowManagerService", "-a", "-a")
        (f.out / "terminated-windows.txt").write_text(rows)
        assert not re.search(r"^\S+\s+\d+\s+" + f.pid + r"\s+", rows, re.M)
        main_accepted = True
        print(
            "PASS actual main close continues the SDK and terminates its Ability",
            flush=True,
        )
    final_faults = faults()
    (f.out / "faults-after.json").write_text(json.dumps(final_faults))
    assert final_faults == original_faults, (original_faults, final_faults)
    report = dict(
        pid=f.pid,
        uid=f.uid,
        kind=f.kind,
        clicks=clicks,
        commands=commands,
        native=r,
        main_sdk_close_terminated=main_accepted,
        new_app_fault_reports=0,
        evidence=str(f.out),
    )
    (f.out / "result.json").write_text(json.dumps(report, indent=2))
    print("NATIVEAPI_NATIVE_CLOSE_PASS " + json.dumps(report), flush=True)


if __name__ == "__main__":
    main()
