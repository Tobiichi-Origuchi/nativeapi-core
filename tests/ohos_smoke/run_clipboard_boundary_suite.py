#!/usr/bin/env python3
"""Actual SDK revisions, concurrent C++/C reads and observer epoch isolation."""

import argparse
import json
from pathlib import Path

from run_clipboard_interop_suite import Interop, MAIN, grant
from ohos_ui_fixture import Fixture

p = argparse.ArgumentParser()
p.add_argument("deveco", type=Path)
p.add_argument("--device", default="127.0.0.1:5555")
p.add_argument("--repro", choices=["pending", "lifetime"])
a = p.parse_args()
c = Interop(a.deveco, a.device)
f = c.f
try:
    if a.repro == "pending":
        c.command("boundary-pending")
    elif a.repro == "lifetime":
        for command in ["lifetime-pending", "lifetime-completed", "lifetime-pending"]:
            c.command(command)
        c.command("lifetime-finish")
    else:
        c.command("publish", peer=True)
        c.command("boundary-burst")
        stable = c.command("boundary-state")
        assert (
            stable["callbacks"] == 30
            and stable["cReleases"] == 15
            and stable["batches"] == 1
        )
        c.command("boundary-pending")
        replaced = c.command("boundary-state")
        assert (
            replaced["callbacks"] == 60
            and replaced["cReleases"] == 30
            and replaced["batches"] == 2
        )
        c.command("boundary-reference")
        before = c.command("boundary-listen")
        assert before["cpp"] == before["c"] == 0 and before["monitoring"]
        c.command("publish", peer=True)
        delivered = c.command("boundary-state")
        assert delivered["cpp"] > 0 and delivered["cpp"] == delivered["c"]
        c.command("boundary-hold")
        c.command("publish", peer=True)
        queued = c.command("boundary-state")
        assert queued["references"] > delivered["references"] and queued["held"] > 0
        assert [queued[k] for k in ["cpp", "c"]] == [delivered[k] for k in ["cpp", "c"]]
        new = c.command("boundary-replace")
        assert [new[k] for k in ["cpp", "c"]] == [queued[k] for k in ["cpp", "c"]]
        assert new["monitoring"] and new["held"] == 0
        c.command("publish", peer=True)
        current = c.command("boundary-state")
        assert current["cpp"] > new["cpp"] and current["c"] > new["c"]
        c.command("boundary-reject-dispatch")
        c.command("publish", peer=True)
        refused_dispatch = c.command("boundary-state")
        assert refused_dispatch["references"] > current["references"]
        assert [refused_dispatch[k] for k in ["cpp", "c"]] == [current[k] for k in ["cpp", "c"]]
        c.command("boundary-restore-dispatch")
        c.command("boundary-self-remove")
        c.command("publish", peer=True)
        removed = c.command("boundary-state")
        assert removed["removed"] == 2 and not removed["monitoring"]
        assert removed["released"] == 2
        c.command("boundary-listen-worker")
        c.command("publish", peer=True)
        worker = c.command("boundary-state")
        assert worker["cpp"] > removed["cpp"] and worker["c"] > removed["c"]
        c.command("boundary-partial")
        c.command("publish", peer=True)
        partial = c.command("boundary-state")
        assert (
            partial["cpp"] > worker["cpp"]
            and partial["c"] == worker["c"]
            and partial["monitoring"]
        )
        c.command("boundary-stop-worker")
        c.command("publish", peer=True)
        stopped = c.command("boundary-state")
        assert (
            stopped["cpp"] == partial["cpp"]
            and stopped["c"] == partial["c"]
            and not stopped["monitoring"]
        )
        # Independent SDK folder publications must expose readable nested bytes.
        directory = c.command("publish-directory", peer=True)
        assert directory["contents"] == ["folder:peer-folder-中文🙂|peer-nested-中文🙂"]
        for command in ["read", "read-c", "read-worker", "files"]:
            folder = c.command(command)
            assert len(folder["paths"]) == 1 and folder["contents"] == directory["contents"]
        for command in ["boundary-publish-directory", "boundary-publish-directory-c"]:
            c.command(command)
            folder = c.command("read", peer=True)
            assert folder["contents"] == ["folder:native-folder-中文🙂|native-nested-中文🙂"]

        c.command("publish", peer=True)
        c.command("boundary-remember-files")
        c.command("delete-files", peer=True)
        c.command("boundary-files-unreadable")
        stale = c.command("read")
        assert not stale["paths"] and not stale["contents"]
        assert stale["text"] == "peer-text-中文🙂"  # Unresolvable URIs are ignored per API contract.

        c.command("publish", peer=True)
        lifetimes = []
        for command in ["lifetime-pending", "lifetime-completed", "lifetime-pending"]:
            epoch = c.command(command)
            assert epoch["cleaned"] == len(lifetimes) + 1 and epoch["released"] == 30
            assert epoch["callbacks"] == (30 if command == "lifetime-completed" else 0)
            if lifetimes:
                assert epoch["generation"] > lifetimes[-1]["generation"]
            lifetimes.append(epoch)
        lifetime_final = c.command("lifetime-finish")
        c.command("publish", peer=True)
        functional = c.command("boundary-finish")
        functional_evidence = str(f.out)
        metadata = f.guest("bm", "dump", "-n", MAIN)
        token = json.loads(metadata[metadata.index("{"):])["applicationInfo"]["accessTokenId"]
        try:
            f.identity()
            f.guest("atm", "perm", "-c", "-i", str(token), "-p", "ohos.permission.READ_PASTEBOARD")
            # The OS kills the consumer on permission revocation. Require a
            # fresh real process and re-check its UID; never reuse the old PID.
            old_pid = f.pid
            f = Fixture(a.deveco, a.device, "clipboard", "CLIPBOARD")
            c.f = f
            f.wait("NATIVEAPI_CLIPBOARD_READY")
            f.identity()
            assert f.pid != old_pid
            c.command("boundary-sdk-denied")
            c.command("boundary-denied")
            denied = c.command("boundary-state")
            assert denied["callbacks"] == 30 and denied["cReleases"] == 15 and denied["batches"] == 1
        finally:
            grant(f, MAIN)
        c.payload(c.command("read-c"), "peer")
        c.command("clear", peer=True)
        final = c.command("boundary-finish")
        assert (
            final["failed"] == 0
            and final["callbacks"] == 30
            and final["cReleases"] == 15
            and functional["released"] == 3
        )
        f.windows("complete")
        f.screenshot("complete")
        result = dict(
            guest=f.instance["name"],
            pid=f.pid,
            peerPid=c.peer_pid,
            uid=f.uid,
            peerUid=c.peer_uid,
            commands=c.sequence,
            native=final,
            functional=functional,
            lifetimes=lifetimes,
            lifetimeFinal=lifetime_final,
            functionalEvidence=functional_evidence,
            evidence=str(f.out),
        )
        (f.out / "boundary-summary.json").write_text(json.dumps(result, indent=2))
        print("NATIVEAPI_CLIPBOARD_BOUNDARY_PASS", json.dumps(result), flush=True)
finally:
    c.command("boundary-finish")
