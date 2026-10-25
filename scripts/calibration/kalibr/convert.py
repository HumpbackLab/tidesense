#!/usr/bin/env python3
"""Convert stereo images and optional IMU, preserving samples and header stamps."""
import argparse
import json
import math
import statistics
import yaml
from pathlib import Path
from rosbags.highlevel import AnyReader
from rosbags.rosbag1 import Writer
from rosbags.typesys import Stores, get_typestore

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('source', type=Path)
p.add_argument('destination', type=Path)
p.add_argument('--topics', nargs=2)
p.add_argument('--cam', type=Path)
p.add_argument('--imu', type=Path)
a = p.parse_args()
cam = None
imu = None
imu_topic = None
if a.cam or a.imu:
    if not (a.cam and a.imu) or a.topics:
        p.error('use --cam and --imu together, without --topics')
    cam = yaml.safe_load(a.cam.read_text())
    imu = yaml.safe_load(a.imu.read_text())
    if not isinstance(cam, dict) or set(cam) != {'cam0', 'cam1'}:
        p.error('camchain must contain exactly cam0 and cam1')
    for key in ('cam0', 'cam1'):
        cfg = cam[key]
        if not isinstance(cfg, dict) or any(k not in cfg for k in
                ('rostopic', 'resolution', 'camera_model', 'distortion_model', 'intrinsics', 'distortion_coeffs')):
            p.error(f'incomplete camera calibration: {key}')
        if not isinstance(cfg['rostopic'], str) or not cfg['rostopic'].startswith('/'):
            p.error(f'invalid rostopic in {key}')
    transform = cam['cam1'].get('T_cn_cnm1')
    if not isinstance(transform, list) or len(transform) != 4 or any(
            not isinstance(row, list) or len(row) != 4 or any(
                not isinstance(v, (int, float)) or not math.isfinite(v) for v in row)
            for row in transform):
        p.error('cam1 requires a finite 4x4 T_cn_cnm1 stereo transform')
    if not isinstance(imu, dict):
        p.error('IMU configuration must be a mapping')
    for key in ('update_rate', 'accelerometer_noise_density', 'accelerometer_random_walk',
                'gyroscope_noise_density', 'gyroscope_random_walk'):
        value = imu.get(key)
        if isinstance(value, bool) or not isinstance(value, (int, float)) or not math.isfinite(value) or value <= 0:
            p.error(f'IMU {key} must be a measured positive finite number (not null)')
    imu_topic = imu.get('rostopic')
    if not isinstance(imu_topic, str) or not imu_topic.startswith('/'):
        p.error('IMU rostopic must be an absolute ROS topic')
    a.topics = [cam['cam0']['rostopic'], cam['cam1']['rostopic']]
if not a.topics:
    p.error('--topics or --cam/--imu are required')
if a.topics[0] == a.topics[1]:
    p.error('left and right topics must differ')
if imu_topic in a.topics:
    p.error('IMU and camera topics must differ')
types = {t: 'sensor_msgs/msg/Image' for t in a.topics}
if imu_topic:
    types[imu_topic] = 'sensor_msgs/msg/Imu'
imu_stamps = []
ros2 = get_typestore(Stores.ROS2_HUMBLE)
ros1 = get_typestore(Stores.ROS1_NOETIC)
stamps = {t: set() for t in a.topics}
last = {t: -1 for t in types}
shapes = {}
receipt_offsets = []
with AnyReader([a.source], default_typestore=ros2) as reader:
    connections = [c for c in reader.connections if c.topic in types]
    if {c.topic for c in connections} != set(types):
        raise SystemExit('Missing requested topics: ' + str(set(types) - {c.topic for c in connections}))
    if any(c.msgtype != types[c.topic] for c in connections):
        raise SystemExit('Requested topics must contain Image/Imu messages as configured')
    for c, receipt, raw in reader.messages(connections=connections):
        msg = reader.deserialize(raw, c.msgtype)
        stamp = msg.header.stamp.sec * 10**9 + msg.header.stamp.nanosec
        if stamp < 0 or stamp <= last[c.topic]:
            raise SystemExit(f'Non-increasing/negative header stamp on {c.topic}')
        last[c.topic] = stamp
        if c.topic == imu_topic:
            if not all(math.isfinite(getattr(v, axis)) for v in
                       (msg.angular_velocity, msg.linear_acceleration) for axis in ('x', 'y', 'z')):
                raise SystemExit('Non-finite IMU angular velocity/acceleration')
            imu_stamps.append(stamp)
            continue
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
    if cam:
        for key, topic in zip(('cam0', 'cam1'), a.topics):
            if shapes[topic] != cam[key]['resolution']:
                raise SystemExit(f'{topic}: bag resolution differs from camchain')
        first, final = min(common), max(common)
        overlap = [s for s in imu_stamps if first <= s <= final]
        if len(overlap) < 2:
            raise SystemExit('Insufficient IMU samples overlapping stereo header times')
    with Writer(a.destination) as writer:
        outputs = {t: writer.add_connection(t, typ, typestore=ros1) for t, typ in types.items()}
        for c, receipt, raw in reader.messages(connections=connections):
            msg = reader.deserialize(raw, c.msgtype)
            stamp = msg.header.stamp.sec * 10**9 + msg.header.stamp.nanosec
            if c.topic == imu_topic or stamp in common:
                # Align bag time with acquisition time for Kalibr time-range selection.
                # ROS1 is already serialized correctly. ROS2 conversion supplies
                # Header.seq=0; existing ROS1 headers (including seq) stay intact.
                serialized = ros2.cdr_to_ros1(raw, c.msgtype) if reader.is2 else raw
                writer.write(outputs[c.topic], stamp, serialized)
summary = dict(source=str(a.source), source_format='ros2' if reader.is2 else 'ros1',
               topics=a.topics, resolution=shapes,
               input_messages={t: len(stamps[t]) for t in a.topics},
               stereo_pairs=len(common), dropped_unpaired={t: len(stamps[t])-len(common) for t in a.topics},
               first_header_ns=min(common), last_header_ns=max(common),
               duration_seconds=(max(common)-min(common))/1e9,
               receipt_minus_header_seconds=[min(receipt_offsets)/1e9, max(receipt_offsets)/1e9],
               bag_time='Image.header.stamp; original Image pixels and header stamps preserved')
if imu_topic:
    intervals = [(y-x)/1e9 for x, y in zip(imu_stamps, imu_stamps[1:])]
    summary['imu'] = dict(topic=imu_topic, messages=len(imu_stamps),
        overlapping_messages=len(overlap), first_header_ns=imu_stamps[0],
        last_header_ns=imu_stamps[-1], configured_rate_hz=imu['update_rate'],
        mean_rate_hz=(len(imu_stamps)-1)*1e9/(imu_stamps[-1]-imu_stamps[0]),
        median_interval_seconds=statistics.median(intervals), max_interval_seconds=max(intervals))
    summary['bag_time'] = 'Each Image/Imu.header.stamp; original pixels, IMU values and headers preserved'
a.destination.with_suffix('.conversion.json').write_text(json.dumps(summary, indent=2)+'\n')
print(json.dumps(summary, indent=2))
