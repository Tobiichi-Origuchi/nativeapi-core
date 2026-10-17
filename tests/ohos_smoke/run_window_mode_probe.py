#!/usr/bin/env python3
"""Independent public SDK window-mode and ordinary-app input capability probe."""

import argparse
import json
import re
from pathlib import Path

from ohos_ui_fixture import Fixture

p = argparse.ArgumentParser()
p.add_argument("deveco", type=Path)
p.add_argument("--device", default="127.0.0.1:5555")
a = p.parse_args()
f = Fixture(a.deveco, a.device, "window-mode-probe", "MODEPROBE")
raw = (f.out / "bundle-metadata.json").read_text()
app = json.loads(raw[raw.index("{") :])["applicationInfo"]
assert app["appPrivilegeLevel"] == "normal" and app["isSystemApp"] is False
records = f.wait("NATIVEAPI_MODEPROBE_DONE")
observations = []
for line in records.splitlines():
    if "NATIVEAPI_MODEPROBE_PHASE " in line:
        match = re.search(r"NATIVEAPI_MODEPROBE_PHASE (\d+) (\S+) (\{.*\})", line)
        assert match
        observations.append(
            dict(index=int(match[1]), phase=match[2], data=json.loads(match[3]))
        )
input_record = next(
    line.split("NATIVEAPI_MODEPROBE_INPUT ", 1)[1]
    for line in records.splitlines()
    if "NATIVEAPI_MODEPROBE_INPUT " in line
)
assert len(observations) == 28
f.tree("final")
f.windows("final")
f.screenshot("final")
summary = dict(
    guest=f.instance["name"],
    pid=f.pid,
    uid=f.uid,
    input=json.loads(input_record),
    observations=observations,
    evidence=str(f.out),
)
(f.out / "summary.json").write_text(json.dumps(summary, indent=2))
print(json.dumps(summary), flush=True)
