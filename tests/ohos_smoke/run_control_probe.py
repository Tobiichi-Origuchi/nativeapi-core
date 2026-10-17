#!/usr/bin/env python3
"""Retain independent SDK title-control/mode observations; never infer core support."""

import argparse
import re
from pathlib import Path

from ohos_ui_fixture import Fixture

p = argparse.ArgumentParser()
p.add_argument("deveco", type=Path)
p.add_argument("--device", default="127.0.0.1:5555")
a = p.parse_args()
f = Fixture(a.deveco, a.device, "control-probe", "CONTROL")
for i in range(15):
    marker = f"NATIVEAPI_CONTROL_PROBE_PHASE {i} "
    records = f.wait(marker)
    line = next(line for line in records.splitlines() if marker in line)
    assert re.search(r" code=\d+ ", line), line
    print(line, flush=True)
    f.tree(str(i))
    f.windows(str(i))
    f.screenshot(str(i))
    f.click("nativeapi-control-probe-next", str(i) + "-next")
f.wait("NATIVEAPI_CONTROL_PROBE_COMPLETE")
print("CONTROL_PROBE_COMPLETE", f.pid, f.out, flush=True)
