#!/usr/bin/env python3
"""Check the real native-DDS image publisher using the camera mock fixture."""
import argparse
import subprocess
import time

import rclpy
from rclpy.qos import qos_profile_sensor_data
from sensor_msgs.msg import CompressedImage
from geometry_msgs.msg import PoseStamped


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("fixture")
    args = parser.parse_args()
    rclpy.init()
    node = rclpy.create_node("jpeg_test_receiver")
    frames = [dict(), dict()]
    poses = set()
    failures = []

    def receive(camera, msg):
        data = bytes(msg.data)
        sequence = data[2] if len(data) == 8 else 0
        expected = bytes([0xff, 0xd8, sequence, camera, 0, 0, 0xff, 0xd9])
        stamp = msg.header.stamp.sec * 1000000000 + msg.header.stamp.nanosec
        if (sequence not in (1, 2, 3) or data != expected or msg.format != "jpeg"
                or msg.header.frame_id != f"cam{camera}_optical_frame"
                or stamp != 2000000000 + sequence * 50000000 + camera * 2600000):
            failures.append((camera, data, stamp))
        frames[camera][sequence] = data

    subscriptions = [node.create_subscription(CompressedImage,
        f"/tidesense/cam{p}/image_raw/compressed", lambda msg, p=p: receive(p, msg),
        qos_profile_sensor_data) for p in range(2)]
    subscriptions.append(node.create_subscription(PoseStamped, "/tidesense/pose",
        lambda msg: poses.add(round(msg.pose.position.x)), qos_profile_sensor_data))
    process = subprocess.Popen([args.fixture], stdin=subprocess.PIPE, text=True)
    try:
        discovery = time.monotonic() + 10
        while time.monotonic() < discovery:
            rclpy.spin_once(node, timeout_sec=0.1)
            if all(node.count_publishers(f"/tidesense/cam{p}/image_raw/compressed") for p in range(2)):
                break
        else:
            raise AssertionError("JPEG publishers were not discovered")
        process.stdin.write("start\n")
        process.stdin.flush()
        deadline = time.monotonic() + 20
        while time.monotonic() < deadline:
            rclpy.spin_once(node, timeout_sec=0.1)
            if process.poll() is not None and all(len(f) == 3 for f in frames) and poses == {1, 2, 3}:
                break
        assert process.wait(timeout=2) == 0, "Fixture failed"
        assert not failures, failures
        assert all(set(f) == {1, 2, 3} for f in frames), frames
        assert poses == {1, 2, 3}, poses
        print("PASS: 6 DDS JPEG payloads, optical frames, original capture times and concurrent pose publication")
    finally:
        if process.poll() is None:
            process.kill()
            process.wait()
        node.destroy_node()
        rclpy.shutdown()


if __name__ == "__main__":
    main()
