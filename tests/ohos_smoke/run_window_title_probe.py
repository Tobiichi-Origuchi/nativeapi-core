#!/usr/bin/env python3
"""Independent SDK child-window action reference; no input is sent."""

import argparse
from pathlib import Path

from ohos_ui_fixture import Fixture

p = argparse.ArgumentParser()
p.add_argument("deveco", type=Path)
p.add_argument("--device", default="127.0.0.1:5555")
a = p.parse_args()
f = Fixture(a.deveco, a.device, "window-title-probe", "TITLEPROBE")
records = f.wait("NATIVEAPI_TITLEPROBE_DONE")
for line in records.splitlines():
    if "NATIVEAPI_TITLEPROBE_PHASE" in line:
        print(line, flush=True)
f.tree("final")
f.windows("final")
f.screenshot("final")
print("TITLEPROBE_COMPLETE", f.pid, f.out, flush=True)
