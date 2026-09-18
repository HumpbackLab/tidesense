#!/usr/bin/env python3
"""Host fixture assertions; use ROS 2 Humble with the same domain as the board."""
import argparse
import json
import time
from pathlib import Path
import rclpy
from rclpy.qos import qos_profile_sensor_data
from geometry_msgs.msg import PoseStamped
from std_msgs.msg import Bool

parser = argparse.ArgumentParser()
parser.add_argument('--seconds', type=float, default=45)
parser.add_argument('--output', required=True)
parser.add_argument('--ready-file')
a = parser.parse_args()
rclpy.init()
node = rclpy.create_node('tidesense_transport_test')
poses, tracking, errors, nodes = [], [], [], set()

def on_pose(msg):
    if not poses:
        print('first pose', time.monotonic(), flush=True)
    p, q = msg.pose.position, msg.pose.orientation
    if (p.x, p.y, p.z, q.x, q.y, q.z, q.w) != (1.25, -2.5, 3.75, 0., 0., 0., 1.):
        errors.append('pose mismatch')
    if msg.header.frame_id != 'odom':
        errors.append('frame mismatch')
    stamp = msg.header.stamp.sec * 10**9 + msg.header.stamp.nanosec
    if stamp < 1700000000000000000 or stamp % 100000000:
        errors.append('timestamp mismatch')
    if poses and stamp <= poses[-1]['stamp']:
        errors.append('timestamps not increasing')
    poses.append({'received': time.monotonic(), 'stamp': stamp})

node.create_subscription(PoseStamped, '/tidesense/pose', on_pose, qos_profile_sensor_data)
node.create_subscription(Bool, '/tidesense/tracking', lambda m: tracking.append(m.data), qos_profile_sensor_data)
if a.ready_file:
    Path(a.ready_file).write_text(str(time.monotonic()))
print('receiver ready', time.monotonic(), flush=True)
end = time.monotonic() + a.seconds
next_report = time.monotonic() + 5
while time.monotonic() < end:
    rclpy.spin_once(node, timeout_sec=.1)
    nodes.update(name for name, namespace in node.get_node_names_and_namespaces()
                 if namespace == '/tidesense')
    if time.monotonic() >= next_report:
        print('progress', time.monotonic(), 'poses', len(poses),
              'publishers', [(x.node_name, x.topic_type, str(x.qos_profile))
                             for x in node.get_publishers_info_by_topic('/tidesense/pose')], flush=True)
        next_report = time.monotonic() + 5
node.destroy_node()
rclpy.shutdown()
result = {'poses': poses, 'tracking_true': tracking.count(True),
          'tracking_false': tracking.count(False), 'nodes': sorted(nodes), 'errors': errors}
with open(a.output, 'w') as f:
    json.dump(result, f, indent=2)
assert len(poses) >= 10, result
assert True in tracking and False in tracking, result
assert 'tidesense' in nodes, result
assert not errors, result
print('micro-ROS receiver PASS:', len(poses), 'poses; tracking loss and ROS node verified')
