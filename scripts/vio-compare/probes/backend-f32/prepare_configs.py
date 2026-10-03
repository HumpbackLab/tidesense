#!/usr/bin/env python3
"""Clone identical calibration/options with separate stage timing outputs."""
import argparse
from pathlib import Path
import re
import shutil

p = argparse.ArgumentParser()
p.add_argument("source", type=Path)
p.add_argument("output", type=Path)
p.add_argument("--name", default="mild")
p.add_argument("--runtime-root", default="/mnt/data/vio-compare/backend-f32")
p.add_argument("--variants", nargs="+", default=["original", "mixed", "fp32"])
args = p.parse_args()
for mode in args.variants:
    dest = args.output / f"config-{args.name}-{mode}"
    dest.mkdir(parents=True, exist_ok=True)
    for path in args.source.glob("*.yaml"):
        shutil.copy2(path, dest / path.name)
    path = dest / "estimator_config.yaml"
    text = path.read_text()
    text, n1 = re.subn(r"^record_timing_information:.*$", "record_timing_information: true", text, flags=re.M)
    stage = f"{args.runtime_root}/results-{args.name}/stages-{mode}.csv"
    text, n2 = re.subn(r"^record_timing_filepath:.*$", f'record_timing_filepath: "{stage}"', text, flags=re.M)
    if n1 != 1 or n2 != 1:
        raise RuntimeError("expected timing settings in config")
    path.write_text(text)
