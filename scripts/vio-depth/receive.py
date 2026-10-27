#!/usr/bin/env python3
"""Validate, join and measure pose/depth/CameraInfo using exact acquisition stamps."""
import argparse
import json
import math
import time
from pathlib import Path

import numpy as np
import rclpy
from geometry_msgs.msg import PoseStamped
from sensor_msgs.msg import Image, CameraInfo
from rclpy.qos import QoSProfile, ReliabilityPolicy


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--seconds', type=float, default=30)
    parser.add_argument('--output', type=Path, help='NEW directory for JSON and lossless depth PNGs')
    parser.add_argument('--min-pairs', type=int, default=1)
    parser.add_argument('--require-hz', type=float, default=0)
    args = parser.parse_args()
    if not math.isfinite(args.seconds) or args.seconds <= 0 or args.min_pairs < 0 or not 0 <= args.require_hz <= 100:
        parser.error('Invalid duration, pair count or rate')
    if args.output:
        import cv2
        args.output.mkdir(parents=True, exist_ok=False)
    rclpy.init()
    node = rclpy.create_node('tidesense_vio_depth_receiver')
    qos = QoSProfile(depth=20, reliability=ReliabilityPolicy.RELIABLE)
    pending = {}
    depth_rows, pose_rows, joined = [], [], []
    errors = []
    camera_info = None

    def stamp(msg):
        return msg.header.stamp.sec * 10**9 + msg.header.stamp.nanosec

    def receive(msg, kind):
        nonlocal camera_info
        t = stamp(msg)
        received = time.monotonic()
        if kind == 'image':
            if (msg.width, msg.height, msg.step, msg.encoding, msg.is_bigendian, len(msg.data), msg.header.frame_id) != (
                    320, 200, 640, '16UC1', 0, 128000, 'depth_optical_frame'):
                errors.append('Invalid depth metadata')
                return
            z = np.frombuffer(msg.data, dtype='<u2').reshape(200, 320)
            valid = z[z > 0]
            depth_rows.append(dict(stamp_ns=t, received=received, valid_pixels=int(valid.size),
                                   median_mm=float(np.median(valid)) if valid.size else None))
            if args.output:
                if not cv2.imwrite(str(args.output / f'{t}.png'), z):
                    raise RuntimeError('Depth save failed')
        elif kind == 'pose':
            p, q = msg.pose.position, msg.pose.orientation
            values = [p.x, p.y, p.z, q.x, q.y, q.z, q.w]
            if msg.header.frame_id != 'odom' or not all(map(math.isfinite, values)) or abs(sum(v*v for v in values[3:])-1) > 1e-5:
                errors.append('Invalid pose')
                return
            pose_rows.append(dict(stamp_ns=t, received=received, pose=values))
        else:
            if (msg.width, msg.height, msg.header.frame_id) != (320, 200, 'depth_optical_frame') or msg.k[0] <= 0 or msg.k[4] <= 0 or any(msg.d):
                errors.append('Invalid CameraInfo')
                return
            camera_info = dict(K=list(msg.k), D=list(msg.d), R=list(msg.r), P=list(msg.p), frame_id=msg.header.frame_id)
        entry = pending.setdefault(t, {})
        if kind in entry:
            errors.append('Duplicate timestamp in '+kind)
        entry[kind] = received
        if all(key in entry for key in ('image', 'info', 'pose')):
            joined.append(dict(stamp_ns=t, received=max(entry.values()), spread_ms=(max(entry.values())-min(entry.values()))*1000))
            del pending[t]
        while len(pending) > 200:
            pending.pop(next(iter(pending)))

    subscriptions = [
        node.create_subscription(Image, '/tidesense/depth/image_raw', lambda m: receive(m, 'image'), qos),
        node.create_subscription(CameraInfo, '/tidesense/depth/camera_info', lambda m: receive(m, 'info'), qos),
        node.create_subscription(PoseStamped, '/tidesense/pose', lambda m: receive(m, 'pose'), qos),
    ]
    print('VIO + depth receiver ready', flush=True)
    begin = time.monotonic()
    try:
        while rclpy.ok() and time.monotonic()-begin < args.seconds:
            rclpy.spin_once(node, timeout_sec=0.1)
    except KeyboardInterrupt:
        pass
    finally:
        node.destroy_node()
        rclpy.shutdown()

    def stats(rows):
        if len(rows) < 2:
            return dict(count=len(rows), acquisition_hz=0, received_hz=0)
        stamps = np.array([r['stamp_ns'] for r in rows], dtype=np.int64)
        intervals = np.diff(stamps)/1e6
        if np.any(intervals <= 0):
            errors.append('Non-increasing timestamps')
        return dict(count=len(rows), acquisition_hz=(len(rows)-1)*1e9/int(stamps[-1]-stamps[0]),
                    received_hz=(len(rows)-1)/(rows[-1]['received']-rows[0]['received']),
                    interval_p50_ms=float(np.median(intervals)), interval_p99_ms=float(np.percentile(intervals, 99)))
    summary = dict(depth=stats(depth_rows), pose=stats(pose_rows), joined=stats(joined), errors=errors)
    if args.output:
        (args.output / 'results.json').write_text(json.dumps(dict(summary=summary, camera=camera_info,
            depth=depth_rows, poses=pose_rows, joined=joined), indent=2)+'\n')
    print(json.dumps(summary, indent=2), flush=True)
    if errors or len(joined) < args.min_pairs or summary['joined']['received_hz'] < args.require_hz:
        raise SystemExit(1)


if __name__ == '__main__':
    main()
