#!/usr/bin/env python3
"""Run fixed-input LK comparisons on the board and fetch point-level results.

Requires the compatible LK firmware and exclusive TPU bridge to be loaded first.
Uses SSH keys, or sshpass -e when SSHPASS is set in the environment.
"""
import argparse
import csv
import json
import math
import os
from pathlib import Path
import re
import shlex
import shutil
import statistics
import subprocess


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--dataset", type=Path, required=True)
    p.add_argument("--output", type=Path, required=True)
    p.add_argument("--host", default="root@192.168.123.99")
    p.add_argument("--remote", default="/mnt/data/c906-reload")
    p.add_argument("--known-hosts", type=Path)
    p.add_argument("--frames", default="200,450,700,950,1200,1450,1700,1940")
    p.add_argument("--points", type=int, default=150)
    p.add_argument("--calls", type=int, default=3)
    a = p.parse_args()
    a.output.mkdir(parents=True, exist_ok=True)
    inputs = a.output / "lk-pairs"
    inputs.mkdir(exist_ok=True)
    with (a.dataset / "stereo.csv").open() as f:
        rows = [r for r in csv.reader(f) if r and not r[0].startswith("#")]
    options = ["-o", "ConnectTimeout=10"]
    if a.known_hosts:
        options += ["-o", f"UserKnownHostsFile={a.known_hosts}"]
    prefix = ["sshpass", "-e"] if os.environ.get("SSHPASS") else []
    ssh = [*prefix, "ssh", *options, a.host]
    scp = [*prefix, "scp", "-O", *options]

    def remote(command):
        return subprocess.run([*ssh, "set -e\n" + shlex.join(command)],
                              check=True, capture_output=True, text=True)

    cases = []
    for frame in map(int, a.frames.split(",")):
        if frame < 0 or frame + 1 >= len(rows):
            raise ValueError(f"frame {frame} is outside the recording")
        for camera in (0, 1):
            names = []
            for index in (frame, frame + 1):
                name = f"image_{camera}_{index}.pgm"
                shutil.copyfile(a.dataset / rows[index][camera + 1], inputs / name)
                names.append(name)
            cases.append(dict(frame=frame, camera=camera, prev=names[0], next=names[1]))
    (a.output / "manifest.json").write_text(json.dumps(cases, indent=2) + "\n")
    remote(["mkdir", "-p", a.remote])
    subprocess.run([*scp, "-r", str(inputs), f"{a.host}:{shlex.quote(a.remote)}/"], check=True)
    results, distances, outliers = [], [], []
    for case in cases:
        name = f"fpv-{case['frame']}-{case['camera']}"
        command = [a.remote + "/c906-tpu-lk", a.remote + "/lk-pairs/" + case["prev"],
                   a.remote + "/lk-pairs/" + case["next"], str(a.points), str(a.calls),
                   a.remote + "/" + name + ".csv"]
        r = remote(command)
        (a.output / (name + ".log")).write_text(r.stdout + r.stderr)
        line = next(x for x in r.stdout.splitlines() if x.startswith("SUMMARY"))
        values = {k: float(v) for k, v in re.findall(r"(\w+)=([-+\w.]+)", line)}
        values = {k: (v if math.isfinite(v) else None) for k, v in values.items()}
        results.append(dict(**case, **values))
        print(name, line, flush=True)
        dest = a.output / (name + ".csv")
        subprocess.run([*scp, f"{a.host}:{shlex.quote(a.remote + '/' + name + '.csv')}", str(dest)], check=True)
        with dest.open() as f:
            for row in csv.DictReader(f):
                if int(row["opencv_valid"]) and int(row["tpu_valid"]):
                    error = math.hypot(float(row["opencv_x"]) - float(row["tpu_x"]),
                                       float(row["opencv_y"]) - float(row["tpu_y"]))
                    distances.append(error)
                    if error > .1:
                        outliers.append(dict(frame=case["frame"], camera=case["camera"], epe=error, **row))
    distances.sort()
    baseline = statistics.mean(r["pyramid_ms"] + r["cv_lk_ms"] for r in results)
    prepare = statistics.mean(r["offload_pyramid_ms"] + r["pack_ms"] + r["flush_ms"] for r in results)
    wait = statistics.mean(r["remote_wall_ms"] for r in results)
    aggregate = dict(pairs=len(results), points=sum(r["points"] for r in results),
                     status_agree=sum(r["status_agree"] for r in results), shared=len(distances),
                     opencv_pyramid_lk_ms=baseline, a53_prepare_ms=prepare, remote_wall_ms=wait,
                     offload_total_ms=prepare + wait, reduction_percent=100 * (baseline - prepare - wait) / baseline,
                     epe_median=statistics.median(distances), epe95=distances[int((len(distances)-1)*.95)],
                     epe99=distances[int((len(distances)-1)*.99)], epe_max=max(distances),
                     over_01px=len(outliers))
    for name, obj in [("summary.json", results), ("aggregate.json", aggregate), ("outliers.json", outliers)]:
        (a.output / name).write_text(json.dumps(obj, indent=2, allow_nan=False) + "\n")
    print(json.dumps(aggregate, indent=2))


if __name__ == "__main__":
    main()
