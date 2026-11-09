#!/usr/bin/env python3
"""Pack matching dark and flat fpn_analyze.py output into camN.fpn LUTs.

Each gain bucket contains the global black level, dark-frame column offsets and
flat-field column gains. Dark and flat captures must cover the same gains and
image dimensions. The output uses TFPN version 2 with the new mandatory
offset/black/gain layout; older version-2 files without the gain vectors are
rejected by the runtime.
"""
import argparse
import os
import pathlib
import struct
import tempfile

import numpy as np

MAGIC = b"TFPN"
VERSION = 2
HEADER_SIZE = 24
FLAG_OFFSET_Q8 = 1
FLAG_BLACK_Q8 = 2
FLAG_GAIN_Q12 = 4
MIN_GAIN_Q12 = 2048
MAX_GAIN_Q12 = 8192


def read_meta(path):
    meta = {}
    for line in path.read_text().splitlines():
        key, sep, value = line.partition("=")
        if sep:
            meta[key.strip()] = value.strip()
    return meta


def load_dark(directory, cam):
    meta = read_meta(directory / "meta.txt")
    if meta.get("mode", "dark") != "dark":
        raise SystemExit(f"{directory}: expected a dark capture")
    width, height = int(meta["width"]), int(meta["height"])
    gain = int(meta["gain_q10"])
    lut_meta = read_meta(directory / f"cam{cam}_lut.txt")
    if int(lut_meta["width"]) != width or int(lut_meta["height"]) != height:
        raise SystemExit(f"{directory}: cam{cam}_lut.txt dimensions differ from meta")
    if int(lut_meta["gain_q10"]) != gain:
        raise SystemExit(f"{directory}: cam{cam}_lut.txt gain differs from meta")
    black = float(lut_meta["black"])
    if not 0.0 <= black <= 255.0:
        raise SystemExit(f"camera {cam}: black level {black} outside 0..255")
    lut_path = directory / f"cam{cam}_lut.f32"
    offset = np.fromfile(lut_path, dtype=np.float32)
    if offset.size != width:
        raise SystemExit(f"{lut_path}: expected {width} values, got {offset.size}")
    if not np.all(np.isfinite(offset)):
        raise SystemExit(f"{lut_path}: contains non-finite values")
    return gain, width, height, black, offset


def load_flat(directory, cam):
    meta = read_meta(directory / "meta.txt")
    if meta.get("mode") != "flat":
        raise SystemExit(f"{directory}: expected a flat capture")
    width, height = int(meta["width"]), int(meta["height"])
    gain = int(meta["gain_q10"])
    flat_meta = read_meta(directory / f"cam{cam}_flat.txt")
    if int(flat_meta["width"]) != width or int(flat_meta["height"]) != height:
        raise SystemExit(f"{directory}: cam{cam}_flat.txt dimensions differ from meta")
    if int(flat_meta["gain_q10"]) != gain:
        raise SystemExit(f"{directory}: cam{cam}_flat.txt gain differs from meta")
    if float(flat_meta.get("flat_signal", "0")) < 8.0:
        raise SystemExit(f"camera {cam}: flat signal is too low")
    flat_path = directory / f"cam{cam}_flat.f32"
    flat_gain = np.fromfile(flat_path, dtype=np.float32)
    if flat_gain.size != width:
        raise SystemExit(f"{flat_path}: expected {width} values, got {flat_gain.size}")
    if not np.all(np.isfinite(flat_gain)):
        raise SystemExit(f"{flat_path}: contains non-finite values")
    return gain, width, height, flat_gain


def pair_buckets(dark_dirs, flat_dirs, cam):
    darks = {}
    flats = {}
    for directory in dark_dirs:
        gain, width, height, black, offset = load_dark(directory, cam)
        if gain in darks:
            raise SystemExit(f"camera {cam}: duplicate dark gain {gain}")
        darks[gain] = (width, height, black, offset)
    for directory in flat_dirs:
        gain, width, height, flat_gain = load_flat(directory, cam)
        if gain in flats:
            raise SystemExit(f"camera {cam}: duplicate flat gain {gain}")
        flats[gain] = (width, height, flat_gain)
    if set(darks) != set(flats):
        missing_flat = sorted(set(darks) - set(flats))
        missing_dark = sorted(set(flats) - set(darks))
        raise SystemExit(f"camera {cam}: dark/flat gains differ; "
                         f"missing flat {missing_flat}, missing dark {missing_dark}")
    buckets = []
    for gain in sorted(darks):
        width, height, black, offset = darks[gain]
        flat_width, flat_height, flat_gain = flats[gain]
        if (flat_width, flat_height) != (width, height):
            raise SystemExit(f"camera {cam}: gain {gain} dimensions differ")
        buckets.append((gain, width, height, black, offset, flat_gain))
    if len(buckets) > 64:
        raise SystemExit(f"camera {cam}: at most 64 gain buckets are supported")
    width, height = buckets[0][1], buckets[0][2]
    if any(bucket[1] != width or bucket[2] != height for bucket in buckets):
        raise SystemExit(f"camera {cam}: capture dimensions differ between gains")
    return width, height, buckets


def encode(width, height, buckets):
    data = bytearray()
    data += struct.pack(
        "<4sHHHHHHII", MAGIC, VERSION, HEADER_SIZE, width, height,
        len(buckets), FLAG_OFFSET_Q8 | FLAG_BLACK_Q8 | FLAG_GAIN_Q12,
        HEADER_SIZE, 0)
    for gain, _width, _height, black, offset, flat_gain in buckets:
        offset_q8 = np.rint(offset * 256.0)
        if offset_q8.size and (offset_q8.min() < -32768 or offset_q8.max() > 32767):
            raise SystemExit(f"column offset for gain {gain} does not fit Q8 int16")
        flat_q12 = np.rint(flat_gain * 4096.0)
        if flat_q12.size and (flat_q12.min() < MIN_GAIN_Q12 or
                              flat_q12.max() > MAX_GAIN_Q12):
            raise SystemExit(f"flat gain for gain {gain} outside Q12 range")
        data += struct.pack("<II", gain, int(round(black * 256.0)))
        data += offset_q8.astype("<i2").tobytes()
        data += flat_q12.astype("<u2").tobytes()
    return bytes(data)


def write_atomic(path, data):
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = None
    try:
        with tempfile.NamedTemporaryFile(
                prefix=f".{path.name}.", suffix=".tmp", dir=path.parent,
                delete=False) as file:
            temporary = pathlib.Path(file.name)
            file.write(data)
            file.flush()
            os.fsync(file.fileno())
        os.replace(temporary, path)
    finally:
        if temporary is not None and temporary.exists():
            temporary.unlink()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--dark", nargs="+", required=True,
                        type=pathlib.Path,
                        help="analyzed dark directories, one per gain")
    parser.add_argument("--flat", nargs="+", required=True,
                        type=pathlib.Path,
                        help="analyzed flat directories, one per gain")
    parser.add_argument("--out", type=pathlib.Path, required=True)
    args = parser.parse_args()
    encoded = {}
    summaries = {}
    for cam in (0, 1):
        width, height, buckets = pair_buckets(args.dark, args.flat, cam)
        encoded[cam] = encode(width, height, buckets)
        summaries[cam] = (width, height, buckets)
    for cam in (0, 1):
        path = args.out / f"cam{cam}.fpn"
        write_atomic(path, encoded[cam])
        width, height, buckets = summaries[cam]
        gains = ", ".join(str(bucket[0]) for bucket in buckets)
        blacks = ", ".join(f"{bucket[3]:.2f}" for bucket in buckets)
        flat_min = min(float(bucket[5].min()) for bucket in buckets)
        flat_max = max(float(bucket[5].max()) for bucket in buckets)
        print(f"camera {cam}: {width}x{height}, gains [{gains}], "
              f"black [{blacks}], flat gain {flat_min:.4f}..{flat_max:.4f} "
              f"-> {path}")


if __name__ == "__main__":
    main()
