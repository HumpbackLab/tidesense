#!/usr/bin/env python3
"""Analyze dark or flat calibration captures and render comparisons.

Column means are captured per frame by tide_cam_fpn_calib. The first half of
the frames forms the calibration set, the second half is held out for
validation.

Dark mode writes black and per-column offsets. Flat mode subtracts the matching
dark calibration, then estimates only the high-frequency per-column response
relative to a local baseline. This preserves broad lens shading while removing
the fixed fine vertical stripes that remain in normally illuminated scenes.
"""
import argparse
import pathlib
import sys

import numpy as np

try:
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
except ImportError:  # Analysis still works without plots.
    plt = None


OFFSET_HALF_SIGNAL = 48.0
MIN_GAIN_Q12 = 2048
MAX_GAIN_Q12 = 8192
DEFAULT_SMOOTH_COLUMNS = 33


def read_meta(path):
    meta = {}
    for line in path.read_text().splitlines():
        key, sep, value = line.partition("=")
        if sep:
            meta[key.strip()] = value.strip()
    return meta


def read_pgm(path):
    data = path.read_bytes()
    if not data.startswith(b"P5"):
        raise ValueError(f"{path}: not a binary PGM")
    pos = 2
    values = []
    while len(values) < 3:
        while pos < len(data) and data[pos : pos + 1].isspace():
            pos += 1
        if pos < len(data) and data[pos : pos + 1] == b"#":
            pos = data.index(b"\n", pos) + 1
            continue
        start = pos
        while pos < len(data) and not data[pos : pos + 1].isspace():
            pos += 1
        values.append(int(data[start:pos]))
    width, height, maxval = values
    if maxval != 255:
        raise ValueError(f"{path}: expected maxval 255, got {maxval}")
    pos += 1
    pixels = np.frombuffer(data, dtype=np.uint8, count=width * height, offset=pos)
    if pixels.size != width * height:
        raise ValueError(f"{path}: truncated PGM")
    return pixels.reshape(height, width)


def center_columns(profile):
    return profile - profile.mean()


def moving_average(profile, columns):
    width = profile.size
    window = max(3, min(int(columns), width))
    if window % 2 == 0:
        window -= 1
    pad = window // 2
    padded = np.pad(profile, pad, mode="edge")
    return np.convolve(padded, np.ones(window) / window, mode="valid")


def stripe_std(profile, columns):
    return float(np.std(profile - moving_average(profile, columns)))


def load_dark(cam, directory, width, height, gain):
    meta_path = directory / "meta.txt"
    lut_meta_path = directory / f"cam{cam}_lut.txt"
    lut_path = directory / f"cam{cam}_lut.f32"
    for path in (meta_path, lut_meta_path, lut_path):
        if not path.exists():
            raise ValueError(f"{path} not found; analyze the matching dark capture first")
    meta = read_meta(meta_path)
    lut_meta = read_meta(lut_meta_path)
    if meta.get("mode", "dark") != "dark":
        raise ValueError(f"{directory}: expected a dark capture, got {meta.get('mode')}")
    if int(meta["gain_q10"]) != gain:
        raise ValueError(f"{directory}: dark gain {meta['gain_q10']} != flat gain {gain}")
    if int(meta["width"]) != width or int(meta["height"]) != height:
        raise ValueError(f"{directory}: dark dimensions do not match the flat capture")
    if int(lut_meta["width"]) != width or int(lut_meta["height"]) != height:
        raise ValueError(f"{lut_meta_path}: dimensions do not match the flat capture")
    if int(lut_meta["gain_q10"]) != gain:
        raise ValueError(f"{lut_meta_path}: gain does not match the flat capture")
    black = float(lut_meta["black"])
    offset = np.fromfile(lut_path, dtype=np.float32)
    if offset.size != width:
        raise ValueError(f"{lut_path}: expected {width} values, got {offset.size}")
    return black, offset


def subtract_dark(cols, black, offset):
    frame_mean = cols.mean(axis=1)
    signal = np.maximum(0.0, frame_mean - black)
    alpha = OFFSET_HALF_SIGNAL / (OFFSET_HALF_SIGNAL + signal)
    return cols - black - alpha[:, None] * offset[None, :]


def render_dark(cam, raw, black, offset, out_dir, before, after):
    """Write a few views with a shared contrast stretch."""
    if plt is None:
        return
    height, width = raw.shape
    corrected = raw.astype(np.float32) - black - offset[None, :]
    band = max(6.0, 3.0 * float(np.std(before)))
    center = float(np.median(corrected))
    vmin, vmax = center - band, center + band

    rows = 10
    usable = (height // rows) * rows
    raw_binned = raw[:usable].reshape(-1, rows, width).mean(axis=1)
    corr_binned = corrected[:usable].reshape(-1, rows, width).mean(axis=1)

    fig, axes = plt.subplots(1, 2, figsize=(14, 4), sharey=True)
    for ax, image, title in (
        (axes[0], raw_binned, "before (row-binned)"),
        (axes[1], corr_binned, "after (row-binned)"),
    ):
        ax.imshow(image, cmap="gray", vmin=vmin, vmax=vmax, aspect="auto",
                  interpolation="nearest")
        ax.set_title(title)
        ax.set_xlabel("column")
    fig.suptitle(f"camera {cam}: black {black:.2f} LSB + column pattern, "
                 f"band +/-{band:.1f} LSB")
    fig.tight_layout()
    fig.savefig(out_dir / f"cam{cam}_stripes.png", dpi=120)
    plt.close(fig)

    fig, ax = plt.subplots(figsize=(14, 3))
    ax.plot(before, lw=0.7, label="before (held-out)")
    ax.plot(after, lw=0.7, label="after (held-out)")
    ax.set_xlabel("column")
    ax.set_ylabel("column mean - image mean (LSB)")
    ax.set_title(f"camera {cam}: column profile, held-out frames")
    ax.grid(alpha=0.2)
    ax.legend()
    fig.tight_layout()
    fig.savefig(out_dir / f"cam{cam}_profile.png", dpi=120)
    plt.close(fig)


def render_flat(cam, raw, black, offset, gain, smooth_columns, out_dir,
                before, after):
    if plt is None:
        return
    height, width = raw.shape
    signal = max(0.0, float(raw.mean()) - black)
    alpha = OFFSET_HALF_SIGNAL / (OFFSET_HALF_SIGNAL + signal)
    dark_corrected = raw.astype(np.float32) - black - alpha * offset[None, :]
    corrected = dark_corrected * gain[None, :]
    before_residual = before - moving_average(before, smooth_columns)
    after_residual = after - moving_average(after, smooth_columns)
    band = max(4.0, 3.0 * float(np.std(before_residual)))
    center = float(np.median(corrected))
    vmin, vmax = center - band, center + band

    rows = 10
    usable = (height // rows) * rows
    raw_binned = dark_corrected[:usable].reshape(-1, rows, width).mean(axis=1)
    corr_binned = corrected[:usable].reshape(-1, rows, width).mean(axis=1)
    fig, axes = plt.subplots(1, 2, figsize=(14, 4), sharey=True)
    for ax, image, title in (
        (axes[0], raw_binned, "before (dark-corrected)"),
        (axes[1], corr_binned, "after flat gain"),
    ):
        ax.imshow(image, cmap="gray", vmin=vmin, vmax=vmax, aspect="auto",
                  interpolation="nearest")
        ax.set_title(title)
        ax.set_xlabel("column")
    fig.suptitle(f"camera {cam}: flat-field column gain, "
                 f"band +/-{band:.1f} LSB")
    fig.tight_layout()
    fig.savefig(out_dir / f"cam{cam}_flat_stripes.png", dpi=120)
    plt.close(fig)

    fig, axes = plt.subplots(1, 2, figsize=(14, 3))
    axes[0].plot(before_residual, lw=0.7, label="before (held-out)")
    axes[0].plot(after_residual, lw=0.7, label="after (held-out)")
    axes[0].set_xlabel("column")
    axes[0].set_ylabel("local residual (LSB)")
    axes[0].set_title(f"camera {cam}: high-frequency column residual")
    axes[0].grid(alpha=0.2)
    axes[0].legend()
    axes[1].plot(gain, lw=0.7)
    axes[1].set_xlabel("column")
    axes[1].set_ylabel("gain")
    axes[1].set_title(f"flat gain, {smooth_columns}-column baseline")
    axes[1].grid(alpha=0.2)
    fig.tight_layout()
    fig.savefig(out_dir / f"cam{cam}_flat_profile.png", dpi=120)
    plt.close(fig)


def analyze_dark(cam, cols, meta, out_dir):
    frames, width = cols.shape
    split = frames // 2
    if split < 2 or frames - split < 2:
        raise ValueError(f"camera {cam}: need at least 4 frames, got {frames}")
    cal = cols[:split]
    val = cols[split:]
    cal_profile = cal.mean(axis=0)
    val_profile = val.mean(axis=0)
    black = float(cal_profile.mean())
    val_black = float(val_profile.mean())
    offset = center_columns(cal_profile)
    before = center_columns(val_profile)
    after = before - offset
    after_black = val_black - black + float(after.mean())

    before_std = float(np.std(before))
    after_std = float(np.std(after))
    reduction = 100.0 * (1.0 - after_std / before_std) if before_std else 0.0
    temporal = float(np.mean(np.std(cal, axis=0)))
    level = float(cols.mean())
    lut_path = out_dir / f"cam{cam}_lut.f32"
    offset.astype(np.float32).tofile(lut_path)
    (out_dir / f"cam{cam}_lut.txt").write_text(
        f"width={width}\nheight={int(meta['height'])}\n"
        f"gain_q10={meta.get('gain_q10')}\n"
        f"exposure_us={meta.get('exposure_us')}\n"
        f"black={black:.6f}\nblack_q8={int(round(black * 256))}\n"
        f"source_frames={frames}\ncalibration_frames={split}\n"
        f"offset_q8_min={int(round(offset.min()*256))}\n"
        f"offset_q8_max={int(round(offset.max()*256))}\n")

    print(f"camera {cam}: black={black:.3f} LSB "
          f"(held-out {val_black:.3f} -> {after_black:.3f}), "
          f"level={level:.2f} LSB, "
          f"temporal_std={temporal:.3f} LSB")
    print(f"  column pattern std: before={before_std:.3f} "
          f"after={after_std:.3f} LSB, reduction={reduction:.1f}%")
    print(f"  offset range: {offset.min():+.2f}..{offset.max():+.2f} LSB, "
          f"LUT={lut_path}")
    if black > 64.0:
        print(f"  warning: dark level {black:.1f} LSB is high; "
              f"check lens cover and ISP BLC", file=sys.stderr)

    try:
        raw = read_pgm(out_dir / f"cam{cam}_last.pgm").astype(np.float32)
    except (OSError, ValueError) as exc:
        print(f"  (image comparison skipped: {exc})", file=sys.stderr)
        return
    render_dark(cam, raw, black, offset, out_dir, before, after)


def analyze_flat(cam, cols, meta, dark_dir, smooth_columns, out_dir):
    frames, width = cols.shape
    height = int(meta["height"])
    gain_q10 = int(meta["gain_q10"])
    split = frames // 2
    if split < 2 or frames - split < 2:
        raise ValueError(f"camera {cam}: need at least 4 frames, got {frames}")
    black, offset = load_dark(cam, dark_dir, width, height, gain_q10)
    cal = subtract_dark(cols[:split], black, offset)
    val = subtract_dark(cols[split:], black, offset)
    cal_profile = cal.mean(axis=0)
    val_profile = val.mean(axis=0)

    saturation = float(meta.get(f"cam{cam}_saturated_fraction", "nan"))
    if not np.isfinite(saturation):
        try:
            saturation = float(np.mean(read_pgm(
                out_dir / f"cam{cam}_last.pgm") == 255))
        except (OSError, ValueError):
            saturation = 0.0
    if cal_profile.min() <= 8.0:
        raise ValueError(f"camera {cam}: flat level is too low; increase "
                         f"illumination or exposure")
    if saturation > 0.01:
        raise ValueError(f"camera {cam}: {saturation*100:.2f}% of flat pixels "
                         f"are clipped; reduce exposure")
    if saturation > 0.001:
        print(f"  warning: camera {cam}: {saturation*100:.2f}% of flat pixels "
              f"are clipped", file=sys.stderr)

    if smooth_columns >= width:
        raise ValueError(f"camera {cam}: --smooth-columns must be smaller than "
                         f"image width {width}")
    baseline = moving_average(cal_profile, smooth_columns)
    if np.any(baseline <= 0):
        raise ValueError(f"camera {cam}: invalid flat baseline")
    gain = baseline / cal_profile
    low, high = MIN_GAIN_Q12 / 4096.0, MAX_GAIN_Q12 / 4096.0
    clamped = int(np.count_nonzero((gain < low) | (gain > high)))
    if clamped > max(2, width // 50):
        raise ValueError(f"camera {cam}: {clamped} columns need gain outside "
                         f"{low:.2f}..{high:.2f}; check illumination uniformity")
    gain = np.clip(gain, low, high)
    gain /= gain.mean()
    gain = np.clip(gain, low, high)
    q12 = np.rint(gain * 4096.0)

    before = val_profile
    after = val_profile * gain
    before_std = stripe_std(before, smooth_columns)
    after_std = stripe_std(after, smooth_columns)
    reduction = 100.0 * (1.0 - after_std / before_std) if before_std else 0.0
    flat_signal = float(cal_profile.mean())
    flat_path = out_dir / f"cam{cam}_flat.f32"
    gain.astype(np.float32).tofile(flat_path)
    (out_dir / f"cam{cam}_flat.txt").write_text(
        f"width={width}\nheight={height}\n"
        f"gain_q10={gain_q10}\nexposure_us={meta.get('exposure_us')}\n"
        f"dark_exposure_us={read_meta(dark_dir / 'meta.txt').get('exposure_us')}\n"
        f"black={black:.6f}\nblack_q8={int(round(black * 256))}\n"
        f"source_frames={frames}\ncalibration_frames={split}\n"
        f"flat_signal={flat_signal:.6f}\n"
        f"flat_signal_min={float(cal_profile.min()):.6f}\n"
        f"flat_signal_max={float(cal_profile.max()):.6f}\n"
        f"smooth_columns={smooth_columns}\n"
        f"stripe_std_before={before_std:.6f}\n"
        f"stripe_std_after={after_std:.6f}\n"
        f"gain_q12_min={int(q12.min())}\ngain_q12_max={int(q12.max())}\n")

    print(f"camera {cam}: flat_signal={flat_signal:.2f} LSB, "
          f"clipped={saturation*100:.3f}%, gain={gain.min():.4f}..{gain.max():.4f}")
    print(f"  high-frequency column std: before={before_std:.3f} "
          f"after={after_std:.3f} LSB, reduction={reduction:.1f}%")
    if clamped:
        print(f"  warning: clamped {clamped} flat-gain columns", file=sys.stderr)
    print(f"  flat gain LUT={flat_path}")

    try:
        raw = read_pgm(out_dir / f"cam{cam}_last.pgm").astype(np.float32)
    except (OSError, ValueError) as exc:
        print(f"  (image comparison skipped: {exc})", file=sys.stderr)
        return
    render_flat(cam, raw, black, offset, gain, smooth_columns, out_dir,
                before, after)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("directory", type=pathlib.Path,
                        help="tide_cam_fpn_calib output directory")
    parser.add_argument("--mode", choices=("dark", "flat"),
                        help="override the mode recorded in meta.txt")
    parser.add_argument("--dark", type=pathlib.Path,
                        help="analyzed dark directory matching a flat capture")
    parser.add_argument("--smooth-columns", type=int,
                        default=DEFAULT_SMOOTH_COLUMNS,
                        help=f"flat local-baseline width (default "
                             f"{DEFAULT_SMOOTH_COLUMNS})")
    args = parser.parse_args()
    out_dir = args.directory
    meta_path = out_dir / "meta.txt"
    if not meta_path.exists():
        parser.error(f"{meta_path} not found (run tide_cam_fpn_calib first)")
    meta = read_meta(meta_path)
    mode = args.mode or meta.get("mode", "dark")
    if args.mode and meta.get("mode") and args.mode != meta["mode"]:
        parser.error(f"{meta_path}: capture mode is {meta['mode']}, not {args.mode}")
    if mode == "flat" and args.dark is None:
        parser.error("--dark is required for flat analysis")
    width = int(meta["width"])
    frames = int(meta["frames"])
    if args.smooth_columns < 3:
        parser.error("--smooth-columns must be at least 3")
    for cam in (0, 1):
        path = out_dir / f"cam{cam}_cols.f32"
        cols = np.fromfile(path, dtype=np.float32)
        if cols.size != frames * width:
            raise SystemExit(f"{path}: expected {frames*width} floats, got {cols.size}")
        cols = cols.reshape(frames, width)
        if mode == "flat":
            analyze_flat(cam, cols, meta, args.dark, args.smooth_columns, out_dir)
        else:
            analyze_dark(cam, cols, meta, out_dir)


if __name__ == "__main__":
    main()
