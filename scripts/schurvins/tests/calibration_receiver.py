#!/usr/bin/env python3
"""Verify real ROS Image messages; optionally check the camera IPC fixture pixels."""
import argparse
import json
import time
from pathlib import Path
import numpy as np
import rclpy
from rclpy.qos import QoSProfile, ReliabilityPolicy, DurabilityPolicy
from sensor_msgs.msg import Image

p = argparse.ArgumentParser()
p.add_argument('--seconds', type=float, default=30)
p.add_argument('--output', required=True, type=Path)
p.add_argument('--ready-file', type=Path)
p.add_argument('--mock', action='store_true')
p.add_argument('--min-pairs', type=int, default=5)
a = p.parse_args()
a.output.mkdir(parents=True, exist_ok=True)
rclpy.init()
node = rclpy.create_node('tidesense_calibration_test')
qos = QoSProfile(depth=5, reliability=ReliabilityPolicy.RELIABLE,
                 durability=DurabilityPolicy.VOLATILE)
messages = [[], []]
pending = [{}, {}]
pairs, errors = [], []


def receive(msg, side):
    t = msg.header.stamp.sec*10**9+msg.header.stamp.nanosec
    expected_frame = ('left' if side == 0 else 'right')+'_camera_optical_frame'
    if (msg.width, msg.height, msg.step, msg.encoding, len(msg.data), msg.is_bigendian,
        msg.header.frame_id) != (640, 400, 640, 'mono8', 256000, 0, expected_frame):
        errors.append('metadata mismatch')
        return
    if messages[side] and t <= messages[side][-1]['stamp']:
        errors.append('non-increasing timestamp')
    pixels = np.frombuffer(msg.data, dtype=np.uint8)
    if a.mock and not np.all(pixels == pixels[0]):
        errors.append('corrupt mock pixels')
    item = {'stamp': t, 'received': time.monotonic(), 'first_pixel': int(pixels[0]),
            'min': int(pixels.min()), 'max': int(pixels.max()), 'mean': float(pixels.mean())}
    if not messages[side]:
        # PGM preserves mono8 exactly, without requiring image encoder libraries.
        (a.output/('left.pgm' if side == 0 else 'right.pgm')).write_bytes(
            b'P5\n640 400\n255\n'+pixels.tobytes())
    messages[side].append(item)
    pending[side][t] = item
    other = pending[1-side].pop(t, None)
    if other:
        pending[side].pop(t)
        if a.mock and item['first_pixel'] + other['first_pixel'] != 250:
            errors.append('left/right pair mismatch')
        pairs.append({'stamp': t, 'received': time.monotonic()})
    if len(pending[side]) > 100:
        pending[side].pop(next(iter(pending[side])))


for side, label in enumerate(('left', 'right')):
    node.create_subscription(Image, '/tidesense/'+label+'/image_raw',
                             lambda m, side=side: receive(m, side), qos)
if a.ready_file:
    a.ready_file.write_text('ready\n')
print('image receiver ready', flush=True)
end = time.monotonic()+a.seconds
while time.monotonic() < end:
    rclpy.spin_once(node, timeout_sec=.1)
node.destroy_node()
rclpy.shutdown()
rate = (len(pairs)-1)*1e9/(pairs[-1]['stamp']-pairs[0]['stamp']) if len(pairs)>1 else 0
result = {'left': messages[0], 'right': messages[1], 'pairs': pairs,
          'pair_rate_hz': rate, 'errors': errors}
(a.output/'received.json').write_text(json.dumps(result, indent=2))
print('pairs:', len(pairs), 'rate:', rate, 'errors:', errors, flush=True)
assert len(pairs) >= a.min_pairs and not errors, 'ROS image validation failed'
