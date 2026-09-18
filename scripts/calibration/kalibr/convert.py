#!/usr/bin/env python3
"""Convert synchronized raw stereo Image topics without changing pixels/header stamps."""
import argparse
import json
from pathlib import Path
from rosbags.highlevel import AnyReader
from rosbags.rosbag1 import Writer
from rosbags.typesys import Stores, get_typestore

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('source', type=Path)
p.add_argument('destination', type=Path)
p.add_argument('--topics', nargs=2, required=True)
a = p.parse_args()
if a.topics[0] == a.topics[1]:
    p.error('left and right topics must differ')
ros2 = get_typestore(Stores.ROS2_HUMBLE)
ros1 = get_typestore(Stores.ROS1_NOETIC)
stamps = {t: set() for t in a.topics}
last = {t: -1 for t in a.topics}
shapes = {}
receipt_offsets = []
with AnyReader([a.source], default_typestore=ros2) as reader:
    connections = [c for c in reader.connections if c.topic in a.topics]
    if {c.topic for c in connections} != set(a.topics):
        raise SystemExit('Missing requested stereo Image topics in ROS 2 bag')
    if any(c.msgtype != 'sensor_msgs/msg/Image' for c in connections):
        raise SystemExit('Both topics must contain sensor_msgs/msg/Image')
    for c, receipt, raw in reader.messages(connections=connections):
        msg = reader.deserialize(raw, c.msgtype)
        stamp = msg.header.stamp.sec * 10**9 + msg.header.stamp.nanosec
        if stamp < 0 or stamp <= last[c.topic]:
            raise SystemExit(f'Non-increasing/negative Image header stamp on {c.topic}')
        if msg.encoding != 'mono8' or msg.step < msg.width or len(msg.data) != msg.step * msg.height:
            raise SystemExit(f'Invalid raw mono8 Image on {c.topic}')
        shape = [msg.width, msg.height]
        if c.topic in shapes and shapes[c.topic] != shape:
            raise SystemExit(f'Resolution changed on {c.topic}')
        shapes[c.topic] = shape
        last[c.topic] = stamp
        stamps[c.topic].add(stamp)
        receipt_offsets.append(receipt - stamp)
    common = stamps[a.topics[0]] & stamps[a.topics[1]]
    if len(common) < 20:
        raise SystemExit(f'Only {len(common)} exact-stamp stereo pairs; at least 20 required')
    with Writer(a.destination) as writer:
        outputs = {t: writer.add_connection(t, 'sensor_msgs/msg/Image', typestore=ros1) for t in a.topics}
        for c, receipt, raw in reader.messages(connections=connections):
            msg = reader.deserialize(raw, c.msgtype)
            stamp = msg.header.stamp.sec * 10**9 + msg.header.stamp.nanosec
            if stamp in common:
                # Align bag time with acquisition time for Kalibr time-range selection.
                # Image headers/pixels remain unchanged; ROS1 Header.seq defaults to zero.
                writer.write(outputs[c.topic], stamp, ros2.cdr_to_ros1(raw, c.msgtype))
summary = dict(source=str(a.source), topics=a.topics, resolution=shapes,
               input_messages={t: len(stamps[t]) for t in a.topics},
               stereo_pairs=len(common), dropped_unpaired={t: len(stamps[t])-len(common) for t in a.topics},
               first_header_ns=min(common), last_header_ns=max(common),
               duration_seconds=(max(common)-min(common))/1e9,
               receipt_minus_header_seconds=[min(receipt_offsets)/1e9, max(receipt_offsets)/1e9],
               bag_time='Image.header.stamp; original Image pixels and header stamps preserved')
a.destination.with_suffix('.conversion.json').write_text(json.dumps(summary, indent=2)+'\n')
print(json.dumps(summary, indent=2))
