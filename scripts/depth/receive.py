#!/usr/bin/env python3
"""Receive paired depth/CameraInfo; optionally preview or save lossless PNGs."""
import argparse
import json
import math
import time
from pathlib import Path

import numpy as np
import rclpy
from rclpy.qos import QoSProfile, ReliabilityPolicy
from sensor_msgs.msg import CameraInfo, Image


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--seconds', type=float, default=0, help='0: until Ctrl-C')
    p.add_argument('--output', type=Path, help='NEW directory for 16-bit depth PNGs and metadata')
    p.add_argument('--preview', action='store_true')
    p.add_argument('--max-metres', type=float, default=5, help='preview colour range')
    p.add_argument('--min-pairs', type=int, default=1)
    p.add_argument('--test-pattern', action='store_true', help='verify test_depth_ros fixture pixels')
    a = p.parse_args()
    if not math.isfinite(a.seconds) or a.seconds < 0 or not math.isfinite(a.max_metres) or a.max_metres <= 0 or a.min_pairs < 1:
        p.error('Invalid duration, preview range or minimum count')
    if a.output or a.preview:
        import cv2
    if a.output:
        a.output.mkdir(parents=True, exist_ok=False)
    rclpy.init()
    node = rclpy.create_node('tidesense_depth_receiver')
    qos = QoSProfile(depth=5, reliability=ReliabilityPolicy.RELIABLE)
    pending_images, pending_info = {}, {}
    frames, errors = [], []
    quit_preview = False

    def stamp(msg):
        return msg.header.stamp.sec * 10**9 + msg.header.stamp.nanosec

    def pair(t):
        nonlocal quit_preview
        if t not in pending_images or t not in pending_info:
            return
        image, received = pending_images.pop(t)
        info = pending_info.pop(t)
        if (image.width, image.height) not in ((320, 200), (640, 400)) or (
                image.encoding != '16UC1' or image.is_bigendian or image.step != image.width * 2 or
                len(image.data) != image.step * image.height or
                info.width != image.width or info.height != image.height or
                image.header.frame_id != 'depth_optical_frame' or info.header.frame_id != image.header.frame_id or
                info.distortion_model != 'plumb_bob' or len(info.d) != 5 or any(info.d) or
                not all(math.isfinite(v) for v in [*info.k, *info.r, *info.p]) or
                info.k[0] <= 0 or info.k[4] <= 0 or info.k[8] != 1 or
                list(info.r) != [1., 0., 0., 0., 1., 0., 0., 0., 1.] or
                info.p[0] != info.k[0] or info.p[5] != info.k[4] or info.p[3] != 0):
            error = 'Invalid depth image/CameraInfo metadata'
            if not errors:
                print(error, image.width, image.height, image.step, image.encoding,
                      image.header.frame_id, info.header.frame_id, info.distortion_model,
                      list(info.d), list(info.k), list(info.r), list(info.p), flush=True)
            errors.append(error)
            return
        if frames and t <= frames[-1]['stamp_ns']:
            errors.append('Non-increasing acquisition timestamp')
        depth = np.frombuffer(image.data, dtype='<u2').reshape(image.height, image.width)
        if a.test_pattern:
            seq, remainder = divmod(t - 1700000000000000000, 100000000)
            expected = (np.arange(depth.size, dtype=np.uint32) + seq * 17).astype(np.uint16)
            if seq < 0 or remainder or not np.array_equal(depth.reshape(-1), expected):
                errors.append('Corrupt fixture pixels or acquisition timestamp')
        valid = depth[depth > 0]
        item = {'stamp_ns': t, 'received_monotonic': received,
                'valid_fraction': float(valid.size / depth.size),
                'median_mm': float(np.median(valid)) if valid.size else None}
        frames.append(item)
        if len(frames) == 1:
            print(f'{image.width}x{image.height} 16UC1 mm; fx={info.k[0]:.3f}, frame={info.header.frame_id}', flush=True)
            if a.output:
                (a.output / 'camera.json').write_text(json.dumps({
                    'width': info.width, 'height': info.height, 'frame_id': info.header.frame_id,
                    'encoding': '16UC1', 'unit': 'millimetres', 'invalid': 0,
                    'K': list(info.k), 'D': list(info.d), 'R': list(info.r), 'P': list(info.p)}, indent=2) + '\n')
        if a.output and not cv2.imwrite(str(a.output / f'{t}.png'), depth):
            raise RuntimeError('Failed to save depth PNG')
        if a.preview:
            scaled = np.clip(depth.astype(np.float32) / (a.max_metres * 1000) * 255, 0, 255).astype(np.uint8)
            colour = cv2.applyColorMap(255 - scaled, cv2.COLORMAP_JET)
            colour[depth == 0] = 0
            cv2.imshow('TideSense depth (black=invalid; q to exit)', colour)
            quit_preview = cv2.waitKey(1) & 255 in (ord('q'), 27)

    def receive_image(msg):
        t = stamp(msg)
        pending_images[t] = (msg, time.monotonic())
        pair(t)
        while len(pending_images) > 10:
            pending_images.pop(next(iter(pending_images)))

    def receive_info(msg):
        t = stamp(msg)
        pending_info[t] = msg
        pair(t)
        while len(pending_info) > 10:
            pending_info.pop(next(iter(pending_info)))

    node.create_subscription(Image, '/tidesense/depth/image_raw', receive_image, qos)
    node.create_subscription(CameraInfo, '/tidesense/depth/camera_info', receive_info, qos)
    print('Depth receiver ready', flush=True)
    begin = time.monotonic()
    try:
        while not quit_preview and (not a.seconds or time.monotonic() - begin < a.seconds):
            rclpy.spin_once(node, timeout_sec=.1)
    except KeyboardInterrupt:
        pass
    finally:
        node.destroy_node()
        rclpy.shutdown()
        if a.preview:
            cv2.destroyAllWindows()
    rate = ((len(frames) - 1) / (frames[-1]['received_monotonic'] - frames[0]['received_monotonic'])
            if len(frames) > 1 else 0)
    result = {'pairs': len(frames), 'received_hz': rate, 'frames': frames, 'errors': errors}
    if a.output:
        (a.output / 'received.json').write_text(json.dumps(result, indent=2) + '\n')
    print(f'pairs={len(frames)} received_hz={rate:.2f} errors={errors}', flush=True)
    if len(frames) < a.min_pairs or errors:
        raise SystemExit('Depth reception validation failed')


if __name__ == '__main__':
    main()
