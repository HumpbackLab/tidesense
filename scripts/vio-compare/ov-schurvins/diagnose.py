#!/usr/bin/env python3
"""Run isolated Schur backend ablations and evaluate fixed-scale position ATE.

Requires prepared configs/inputs and the diagnostic build described in README.md.
All environment switches belong to the experiment patch, not the SDK runtime.
"""
import argparse
from concurrent.futures import ThreadPoolExecutor
import csv
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import time
import zipfile

import numpy as np

ROOT = Path(__file__).resolve().parents[3]
DEFAULT = ROOT / "install/ov-schurvins-validation-20261025"
SWITCHES = {
    "diagnostic": {},
    "stack": {"OV_SCHUR_FIX_STACK": "1"},
    "noise": {"OV_SCHUR_FIX_NOISE": "1"},
    "fixed": {"OV_SCHUR_FIX_STACK": "1", "OV_SCHUR_FIX_NOISE": "1"},
    "fixed-repeat": {"OV_SCHUR_FIX_STACK": "1", "OV_SCHUR_FIX_NOISE": "1"},
    "msckf": {"OV_SCHUR_USE_MSCKF": "1"},
    "original": {},
    "openvins": {},
}


def prepare(output, euroc_archive):
    base = ROOT / "install/vio-compare-20260924-121320/ov-schurvins"
    source = base / "open_vins"
    revision = subprocess.check_output(["git", "-C", str(source), "rev-parse", "HEAD"], text=True).strip()
    if revision != "d988139c0ba3cc51abacb8b5b5acb482a19325ba":
        raise RuntimeError(f"Diagnostic patch requires pinned source, found {revision}")
    if not (output / "source").exists():
        shutil.copytree(source, output / "source",
                        ignore=shutil.ignore_patterns(".git", ".codegraph", "ov_data", "docs"))
        for runner in ("run_record.cpp", "run_record_tidesense.cpp"):
            shutil.copy2(base / runner, output / runner)
        subprocess.run(["patch", "--batch", "-p1", "-i", str(Path(__file__).with_name("diagnostic.patch"))],
                       cwd=output / "source", check=True)
    fish = ROOT / "install/fisheye-benchmark/openvins"
    for name, config in (("euroc", source / "config/euroc_mav"),
                         ("tum", fish / "tum_vi"), ("uzh", fish / "uzhfpv_indoor")):
        dest = output / "configs" / name
        if dest.exists():
            continue
        shutil.copytree(config, dest)
        path = dest / "estimator_config.yaml"; text = path.read_text()
        for key in ("use_fej", "calib_cam_extrinsics", "calib_cam_intrinsics"):
            text, count = re.subn(rf"(?m)^({key}:)\s*(true|false)", r"\1 false", text)
            if count != 1:
                raise RuntimeError(f"Expected exactly one {key} in {path}")
        path.write_text(text)
    inp = output / "inputs/euroc"
    if euroc_archive and not (inp / "stereo.csv").exists():
        inp.mkdir(parents=True, exist_ok=True)
        with zipfile.ZipFile(euroc_archive) as archive:
            archive.extractall(inp)
        shutil.copy2(inp / "mav0/imu0/data.csv", inp / "imu.csv")
        def images(camera):
            return [r for r in csv.reader((inp / f"mav0/{camera}/data.csv").open())
                    if r and not r[0].startswith("#")]
        right = dict(images("cam1"))
        with (inp / "stereo.csv").open("w") as stream:
            stream.write("#timestamp_ns,left,right\n")
            for stamp, left in images("cam0"):
                if stamp in right:
                    stream.write(f"{stamp},mav0/cam0/data/{left},mav0/cam1/data/{right[stamp]}\n")
    print(f"Prepared diagnostic source at {output / 'source'}")


def dataset_paths(output, name):
    base = ROOT / "install/fisheye-benchmark"
    if name == "euroc":
        inp = output / "inputs/euroc"
        return inp, inp / "mav0/state_groundtruth_estimate0/data.csv", 0
    if name == "tum":
        return (base / "openvins/tum-input",
                base / "datasets/dataset-room1_512_16/mav0/mocap0/data.csv", 0)
    return (base / "openvins/uzh-input",
            base / "datasets/uzh-indoor-forward-6/groundtruth.csv", -16_684_572)


def run_case(output, variant, name, frame_limit=0):
    case = output / variant / name
    case.mkdir(parents=True, exist_ok=True)
    if variant == "original":
        binary = ROOT / "install/vio-compare-20260924-121320/ov-schurvins/build-host/run_record"
    elif variant == "openvins":
        binary = ROOT / "install/fisheye-benchmark/openvins/run_fisheye"
    else:
        binary = output / "build/run_record"
    inp, _, _ = dataset_paths(output, name)
    env = {k: v for k, v in os.environ.items() if not k.startswith(("OV_SCHUR_", "OPENVINS_"))}
    env.update(OPENBLAS_NUM_THREADS="1", OMP_NUM_THREADS="1")
    env.update(SWITCHES[variant])
    if variant not in ("original", "openvins", "msckf"):
        env.update(OV_SCHUR_DIAG_CSV=str(case / "information.csv"),
                   OV_SCHUR_LINEAR_CSV=str(case / "first-linearization.csv"))
    cmd = [str(binary), str(output / "configs" / name / "estimator_config.yaml"),
           str(inp), str(case / "trajectory.csv"), str(frame_limit)]
    print(f"START {variant}/{name}", flush=True)
    started = time.monotonic()
    with (case / "run.log").open("w") as stream:
        process = subprocess.run(cmd, env=env, stdout=stream, stderr=subprocess.STDOUT)
    record = dict(command=cmd, exit_code=process.returncode,
                  wall_s=time.monotonic() - started, frame_limit=frame_limit,
                  switches=SWITCHES[variant])
    (case / "run.json").write_text(json.dumps(record, indent=2) + "\n")
    print(f"DONE {variant}/{name}: exit={process.returncode}, wall={record['wall_s']:.1f}s", flush=True)
    return record


def load_truth(path):
    with path.open() as stream:
        rows = [r for r in csv.reader(stream) if r and not r[0].startswith("#")]
    return (np.array([int(r[0]) for r in rows], dtype=np.int64),
            np.array([[float(x) for x in r[1:4]] for r in rows]))


def align(x, y):
    u, _, vh = np.linalg.svd((x-x.mean(0)).T @ (y-y.mean(0)))
    correction = np.eye(3)
    correction[-1, -1] = np.linalg.det(u @ vh)
    return (x-x.mean(0)) @ (u @ correction @ vh) + y.mean(0)


def evaluate_case(output, variant, name):
    case = output / variant / name
    trajectory = case / "trajectory.csv"
    if not trajectory.exists():
        return None
    rows = list(csv.DictReader(trajectory.open()))
    valid = [r for r in rows if r["initialized"] == "1" and r["compute_ms"]
             and all(r[k] for k in ("px", "py", "pz"))]
    inp, truth, offset = dataset_paths(output, name)
    total_frames = sum(1 for line in (inp / "stereo.csv").open() if line.strip() and not line.startswith("#"))
    log = (case / "run.log").read_text(errors="replace")
    result = dict(frames=sum(bool(r["compute_ms"]) for r in rows),
                  incomplete_csv_rows=sum(not r["compute_ms"] for r in rows),
                  input_frames=total_frames, initialized_frames=len(valid),
                  completed=any(line.startswith("DONE ") for line in log.splitlines()),
                  no_triangulated_features=log.count("No features to update after triangulation"),
                  negative_covariance_lines=sum("diagonal at" in line for line in log.splitlines()),
                  indefinite_updates_skipped=log.count("Indefinite information matrix"))
    metadata = case / "run.json"
    if metadata.exists():
        result["exit_code"] = json.loads(metadata.read_text())["exit_code"]
    if not valid:
        return result
    t = np.array([int(r["timestamp_ns"]) + offset for r in valid], dtype=np.int64)
    positions = np.array([[float(r[k]) for k in ("px", "py", "pz")] for r in valid])
    result.update(first_last_displacement_m=float(np.linalg.norm(positions[-1]-positions[0])),
                  estimated_path_length_m=float(np.linalg.norm(np.diff(positions, axis=0), axis=1).sum()),
                  finite_positions=bool(np.isfinite(positions).all()))
    gt, gp = load_truth(truth)
    mask = (t >= gt[0]) & (t <= gt[-1]) & np.isfinite(positions).all(axis=1)
    times = t[mask]
    result["gt_overlap_frames"] = len(times)
    if len(times) >= 20:
        # Subtract an integer origin before interpolation to preserve ns precision.
        y = np.column_stack([np.interp((times-gt[0])*1e-9, (gt-gt[0])*1e-9, gp[:, k]) for k in range(3)])
        x = positions[mask]
        aligned = align(x, y)
        errors = np.linalg.norm(aligned-y, axis=1)
        result.update(ate_rmse_m=float(np.sqrt(np.mean(errors**2))),
                      ate_p95_m=float(np.percentile(errors, 95)),
                      gt_span_s=float((times[-1]-times[0])*1e-9),
                      gt_path_length_m=float(np.linalg.norm(np.diff(y, axis=0), axis=1).sum()),
                      gt_first_timestamp_ns=int(times[0]), gt_last_timestamp_ns=int(times[-1]))
        with (case / "aligned.csv").open("w") as stream:
            writer = csv.writer(stream)
            writer.writerow(["timestamp_ns", "gt_x", "gt_y", "gt_z", "x", "y", "z", "error_m"])
            writer.writerows((int(ts), *g, *a, e) for ts, g, a, e in zip(times, y, aligned, errors))
    diagnostic = case / "information.csv"
    if diagnostic.exists():
        info = list(csv.DictReader(diagnostic.open()))
        if info:
            result["information"] = dict(updates=len(info),
                indefinite_updates=sum(int(r["negative_eigen"]) > 0 for r in info),
                min_eigen=float(min(float(r["min_eigen"]) for r in info)),
                first_update={k: float(v) for k, v in info[0].items()})
    (case / "evaluation.json").write_text(json.dumps(result, indent=2) + "\n")
    return result


def verify_linearization(path):
    """Check actual first-update Jacobians against orthogonal elimination.

    This uses QR/SVD projection independently of the instrumented C++ Schur loop.
    It also recreates the upstream camera loop to expose duplicate elimination.
    """
    data = np.loadtxt(path, delimiter=",", ndmin=2)
    ids = sorted(set(int(row[3]) for row in data))
    index = {pose: 6*i for i, pose in enumerate(ids)}
    n = 6*len(ids)
    raw = np.zeros((n, n)); upstream = np.zeros((n, n)); schur = np.zeros((n, n)); qr = np.zeros((n, n))
    b_raw = np.zeros(n); b_upstream = np.zeros(n); b_schur = np.zeros(n); b_qr = np.zeros(n)
    for fid in sorted(set(data[:, 1])):
        obs = data[data[:, 1] == fid]
        jx = np.zeros((2*len(obs), n)); jf = np.zeros((2*len(obs), 3)); residual = np.zeros(2*len(obs))
        w = {}
        for i, row in enumerate(obs):
            pose = int(row[3]); idx = index[pose]
            a = row[6:18].reshape(2, 6); f = row[18:24].reshape(2, 3)
            jx[2*i:2*i+2, idx:idx+6] = a; jf[2*i:2*i+2] = f; residual[2*i:2*i+2] = row[4:6]
            w[pose] = w.get(pose, np.zeros((6, 3))) + a.T @ f
        h = jx.T @ jx; b = jx.T @ residual; raw += h; b_raw += b
        vi = np.linalg.inv(jf.T @ jf); gv = jf.T @ residual
        schur += h - jx.T @ jf @ vi @ jf.T @ jx
        b_schur += b - jx.T @ jf @ vi @ gv
        u, s, _ = np.linalg.svd(jf, full_matrices=True)
        rank = int((s > s[0]*1e-10).sum()); null = u[:, rank:]
        jnull = null.T @ jx; rnull = null.T @ residual
        qr += jnull.T @ jnull; b_qr += jnull.T @ rnull
        upstream += h; b_upstream += b
        for cam in set(obs[:, 2]):
            cam_obs = obs[obs[:, 2] == cam]
            for m, row in enumerate(cam_obs):
                a = int(row[3]); ia = index[a]; wvi = w[a] @ vi
                b_upstream[ia:ia+6] -= wvi @ gv
                for row_b in cam_obs[m:]:
                    bb = int(row_b[3]); ib = index[bb]; sub = wvi @ w[bb].T
                    upstream[ia:ia+6, ib:ib+6] -= sub
                    if a != bb:
                        upstream[ib:ib+6, ia:ia+6] -= sub.T
    scale = np.linalg.norm(qr)
    result = dict(features=len(set(data[:, 1])), observations=len(data), clones=len(ids),
        upstream_vs_projection_relative_error=float(np.linalg.norm(upstream-qr)/scale),
        corrected_vs_projection_relative_error=float(np.linalg.norm(schur-qr)/scale),
        corrected_gradient_relative_error=float(np.linalg.norm(b_schur-b_qr)/max(np.linalg.norm(b_qr), 1e-12)),
        upstream_min_eigen=float(np.linalg.eigvalsh(upstream).min()),
        corrected_min_eigen=float(np.linalg.eigvalsh(schur).min()),
        projection_min_eigen=float(np.linalg.eigvalsh(qr).min()))
    # Compare the information update with the two purported measurement models.
    rng = np.random.default_rng(42); factor = rng.normal(size=(n, n)); p = factor @ factor.T * 1e-4 + np.eye(n)*1e-3
    expected = np.linalg.solve(np.linalg.inv(p)+qr, b_qr)
    wrong = p @ schur.T @ np.linalg.solve(schur@p@schur.T + np.eye(n), b_schur)
    lam, q = np.linalg.eigh((schur+schur.T)*0.5); keep = lam > max(1e-10, lam.max()*1e-10)
    j = np.sqrt(lam[keep])[:, None]*q[:, keep].T
    res = (q[:, keep].T@b_schur)/np.sqrt(lam[keep])
    whitened = p @ j.T @ np.linalg.solve(j@p@j.T+np.eye(len(res)), res)
    result.update(identity_noise_update_relative_error=float(np.linalg.norm(wrong-expected)/np.linalg.norm(expected)),
                  whitened_update_relative_error=float(np.linalg.norm(whitened-expected)/np.linalg.norm(expected)))
    return result


def summarize(output):
    results = {}
    for variant in SWITCHES:
        for name in ("euroc", "tum", "uzh"):
            result = evaluate_case(output, variant, name)
            if result is not None:
                results[f"{variant}/{name}"] = result
    proofs = {}
    for name in ("euroc", "tum", "uzh"):
        path = output / "diagnostic" / name / "first-linearization.csv"
        if path.exists() and path.stat().st_size:
            proofs[name] = verify_linearization(path)
    (output / "summary.json").write_text(json.dumps(dict(results=results, linear_algebra=proofs), indent=2) + "\n")
    for name, result in results.items():
        ate = result.get("ate_rmse_m", float("nan"))
        print(f"{name:22} frames={result['frames']:4}/{result['input_frames']} init={result['initialized_frames']:4} ATE={ate:.6f}m")
    print(json.dumps(proofs, indent=2))


def plot(output):
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
    variants = {"original": ("Upstream Schur", "#bb3333"),
                "stack": ("Unique-clone Schur", "#d88b19"),
                "fixed": ("Unique clones + whitened noise", "#0072b2"),
                "msckf": ("Same-fork MSCKF", "#009e73")}
    fig, axes = plt.subplots(1, 3, figsize=(15, 4), constrained_layout=True)
    for ax, name, title in zip(axes, ("euroc", "tum", "uzh"),
                               ("EuRoC V1_01_easy", "TUM-VI room1", "UZH-FPV indoor_forward_6")):
        _, gtpath, _ = dataset_paths(output, name); gt, _ = load_truth(gtpath)
        for variant, (label, color) in variants.items():
            path = output / variant / name / "aligned.csv"
            if not path.exists():
                continue
            with path.open() as stream:
                rows = list(csv.DictReader(stream))
            t = [(int(r["timestamp_ns"])-int(gt[0]))*1e-9 for r in rows]
            ax.semilogy(t, [max(float(r["error_m"]), 1e-4) for r in rows], label=label, color=color, linewidth=1)
        ax.set_title(title); ax.set_xlabel("Time from ground-truth start (s)")
        ax.set_ylabel("Position error after SE(3) alignment (m)"); ax.grid(True, alpha=.25)
    axes[-1].legend(fontsize=8)
    fig.savefig(output / "position-errors.png", dpi=180)
    plt.close(fig)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("action", choices=("prepare", "run", "summarize", "plot"))
    parser.add_argument("--output", type=Path, default=DEFAULT)
    parser.add_argument("--variants", nargs="+", choices=tuple(SWITCHES), default=["diagnostic", "stack", "fixed", "msckf"])
    parser.add_argument("--datasets", nargs="+", choices=("euroc", "tum", "uzh"), default=["euroc", "tum", "uzh"])
    parser.add_argument("--jobs", type=int, default=1)
    parser.add_argument("--frame-limit", type=int, default=0)
    parser.add_argument("--euroc-archive", type=Path)
    args = parser.parse_args(); output = args.output.resolve()
    if args.action == "prepare":
        prepare(output, args.euroc_archive)
    elif args.action == "run":
        tasks = [(v, n) for v in args.variants for n in args.datasets]
        with ThreadPoolExecutor(max_workers=args.jobs) as pool:
            list(pool.map(lambda task: run_case(output, *task, args.frame_limit), tasks))
    elif args.action == "summarize":
        summarize(output)
    else:
        plot(output)


if __name__ == "__main__":
    main()
