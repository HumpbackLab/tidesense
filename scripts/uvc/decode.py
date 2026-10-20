#!/usr/bin/env python3
"""Decode TideSense APP15 metadata from original MJPEG bytes (before image decoding)."""
import argparse
import csv
import json
import struct
from pathlib import Path

HEADER = struct.Struct("<8sHHHHQQQIIIIIQQddI")
SAMPLE = struct.Struct("<QQ6hI")
MAX_JPEG = 524288


def exact(stream, size):
    data = stream.read(size)
    if len(data) != size:
        raise ValueError("truncated JPEG")
    return data


def iter_jpegs(stream):
    """Respect marker lengths: IMU bytes may themselves contain FF D8/FF D9."""
    while True:
        start = stream.read(2)
        if not start:
            return
        if start != b"\xff\xd8":
            raise ValueError("expected JPEG SOI")
        frame = bytearray(start)
        entropy = False
        while True:
            # Skip ordinary entropy bytes in blocks. Walking a full-rate
            # MJPEG capture one Python byte at a time is unnecessarily slow.
            if entropy and hasattr(stream, "peek"):
                pending = stream.peek(65536)
                marker_at = pending.find(b"\xff")
                count = marker_at if marker_at >= 0 else len(pending)
                if count:
                    frame += exact(stream, count)
                    if len(frame) > MAX_JPEG:
                        raise ValueError("oversized JPEG")
                    continue
            first = exact(stream, 1)
            frame += first
            if first != b"\xff":
                if entropy:
                    if len(frame) > MAX_JPEG:
                        raise ValueError("oversized JPEG")
                    continue
                raise ValueError("invalid JPEG marker")
            marker = exact(stream, 1)
            frame += marker
            while marker == b"\xff":
                marker = exact(stream, 1)
                frame += marker
            code = marker[0]
            if code == 0 and entropy:
                continue
            if code == 0xD9:
                yield bytes(frame)
                break
            if 0xD0 <= code <= 0xD7 or code == 1:
                continue
            if code in (0, 0xD8):
                raise ValueError("invalid JPEG marker sequence")
            length = exact(stream, 2)
            size = int.from_bytes(length, "big")
            if size < 2 or len(frame) + size > MAX_JPEG:
                raise ValueError("invalid/oversized JPEG segment")
            frame += length + exact(stream, size - 2)
            entropy = code == 0xDA


def metadata(jpeg):
    if not jpeg.startswith(b"\xff\xd8") or not jpeg.endswith(b"\xff\xd9"):
        raise ValueError("invalid JPEG")
    offset = 2
    while offset + 4 <= len(jpeg):
        if jpeg[offset] != 0xFF:
            raise ValueError("invalid JPEG marker")
        code = jpeg[offset + 1]
        if code in (0xDA, 0xD9):
            break
        size = int.from_bytes(jpeg[offset + 2:offset + 4], "big")
        if size < 2 or offset + 2 + size > len(jpeg):
            raise ValueError("invalid JPEG segment length")
        payload = jpeg[offset + 4:offset + 2 + size]
        offset += 2 + size
        if code != 0xEF or not payload.startswith(b"TIDESNS\0"):
            continue
        if len(payload) < HEADER.size:
            raise ValueError("truncated TideSense header")
        (magic, version, header_size, sample_size, flags, seq, left, right,
         left_ref, right_ref, camera_hz, imu_hz, count, drops, skipped,
         accel_scale, gyro_scale, reserved) = HEADER.unpack_from(payload)
        if (version, header_size, sample_size) != (1, HEADER.size, SAMPLE.size):
            raise ValueError("unsupported TideSense protocol version/layout")
        if count > 1024 or len(payload) != header_size + count * sample_size:
            raise ValueError("invalid IMU sample count/APP15 length")
        if not left or not right or not flags & 1 or (camera_hz, imu_hz) != (40, 400):
            raise ValueError("invalid timestamps/clock/rates")
        if not (0 < accel_scale < 1 and 0 < gyro_scale < 1) or reserved:
            raise ValueError("invalid scales/reserved field")
        samples = []
        for values in SAMPLE.iter_unpack(payload[header_size:]):
            sample_seq, stamp, *axes, sample_flags = values
            samples.append(dict(seq=sample_seq, timestamp_ns=stamp, raw=axes,
                                accel_m_s2=[v * accel_scale for v in axes[:3]],
                                gyro_rad_s=[v * gyro_scale for v in axes[3:]], flags=sample_flags))
        return dict(version=version, frame_seq=seq, left_vi_ns=left, right_vi_ns=right,
                    left_ref=left_ref, right_ref=right_ref, camera_hz=camera_hz, imu_hz=imu_hz,
                    flags=flags, imu_ring_drops=drops, skipped_frames=skipped,
                    accel_scale=accel_scale, gyro_scale=gyro_scale, imu=samples)
    raise ValueError("TideSense APP15 metadata missing")


def decode(path, output=None, extract=False):
    frames = samples = gaps = 0
    first_frame = last_frame = first_imu = last_imu = last_seq = last_sample = None
    max_gap = ring_drops = skipped = 0
    frame_file = imu_file = None
    if output:
        output.mkdir(parents=True, exist_ok=True)
        frame_file = (output / "frames.jsonl").open("w")
        imu_file = (output / "imu.csv").open("w", newline="")
        writer = csv.writer(imu_file)
        writer.writerow(["seq", "timestamp_ns", "ax_raw", "ay_raw", "az_raw",
                         "gx_raw", "gy_raw", "gz_raw", "flags"])
    try:
        with path.open("rb") as stream:
            for jpeg in iter_jpegs(stream):
                meta = metadata(jpeg)
                if last_seq is not None and (meta["frame_seq"] <= last_seq or
                                            meta["left_vi_ns"] <= last_frame):
                    raise ValueError("image timestamp/sequence moved backwards")
                last_seq = meta["frame_seq"]
                if first_frame is None:
                    first_frame = meta["left_vi_ns"]
                last_frame = meta["left_vi_ns"]
                for sample in meta["imu"]:
                    stamp = sample["timestamp_ns"]
                    if not stamp or (last_imu is not None and (stamp <= last_imu or sample["seq"] <= last_sample)):
                        raise ValueError("IMU timestamp/sequence moved backwards or duplicated")
                    if last_imu is not None:
                        max_gap = max(max_gap, stamp - last_imu)
                        gaps += stamp - last_imu > 3750000
                    if first_imu is None:
                        first_imu = stamp
                    last_imu, last_sample = stamp, sample["seq"]
                    if imu_file:
                        writer.writerow([sample["seq"], stamp, *sample["raw"], sample["flags"]])
                    samples += 1
                if frame_file:
                    frame_file.write(json.dumps(meta) + "\n")
                    if extract:
                        (output / f"{meta['frame_seq']:08d}.jpg").write_bytes(jpeg)
                frames += 1
                ring_drops, skipped = meta["imu_ring_drops"], meta["skipped_frames"]
    finally:
        if frame_file:
            frame_file.close()
        if imu_file:
            imu_file.close()
    return dict(frames=frames, imu_samples=samples,
                image_hz=(frames - 1) * 1e9 / (last_frame - first_frame) if frames > 1 else 0,
                imu_hz=(samples - 1) * 1e9 / (last_imu - first_imu) if samples > 1 else 0,
                imu_gap_events=gaps, max_imu_gap_ns=max_gap,
                imu_ring_drops=ring_drops, skipped_frames=skipped)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("input", type=Path, help="concatenated original MJPEG frames")
    parser.add_argument("--output", type=Path, help="write frames.jsonl and imu.csv")
    parser.add_argument("--extract", action="store_true", help="also save JPEGs; requires --output")
    args = parser.parse_args()
    if args.extract and not args.output:
        parser.error("--extract requires --output")
    try:
        print(json.dumps(decode(args.input, args.output, args.extract), indent=2))
    except (OSError, ValueError) as error:
        parser.exit(1, f"TideSense decode failed: {error}\n")
