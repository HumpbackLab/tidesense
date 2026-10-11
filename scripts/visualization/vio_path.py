#!/usr/bin/python3
"""Display a PoseStamped or Odometry stream as a bounded ROS 2 trajectory."""
import argparse
from collections import deque
from copy import deepcopy
import math
import time

import rclpy
from rclpy.node import Node
from rclpy.executors import ExternalShutdownException
from rclpy.qos import DurabilityPolicy, QoSProfile, ReliabilityPolicy, qos_profile_sensor_data
from geometry_msgs.msg import PoseStamped, TransformStamped
from nav_msgs.msg import Odometry, Path
from std_srvs.srv import Trigger
from tf2_ros import TransformBroadcaster


class VioPath(Node):
    def __init__(self, args):
        super().__init__('tidesense_vio_path')
        self.history = deque(maxlen=args.max_points)
        self.latest = None
        self.consumed_stamp = None
        self.dirty = False
        self.next_path_publish = 0.0
        self.child_frame = args.child_frame
        qos = QoSProfile(depth=1, reliability=ReliabilityPolicy.RELIABLE,
                         durability=DurabilityPolicy.TRANSIENT_LOCAL)
        self.path_pub = self.create_publisher(Path, '/tidesense/path', qos)
        self.pose_pub = self.create_publisher(PoseStamped, '/tidesense/viz_pose', qos)
        self.tf = TransformBroadcaster(self)
        msg_type = PoseStamped if args.input_type == 'pose' else Odometry
        self.subscription = self.create_subscription(msg_type, args.input_topic,
                                                      self.on_pose, qos_profile_sensor_data)
        self.clear_service = self.create_service(Trigger, '/tidesense/clear_path', self.clear)
        self.timer = self.create_timer(1.0 / args.rate, self.tick)
        self.get_logger().info(f'Input: {args.input_topic} ({msg_type.__name__}); '
                               f'path: /tidesense/path; history: {args.max_points} points')

    @staticmethod
    def stamp(msg):
        return msg.header.stamp.sec * 1_000_000_000 + msg.header.stamp.nanosec

    def on_pose(self, msg):
        if not msg.header.frame_id:
            return
        pose = msg.pose if isinstance(msg, PoseStamped) else msg.pose.pose
        p, q = pose.position, pose.orientation
        if not all(math.isfinite(v) for v in (p.x, p.y, p.z, q.x, q.y, q.z, q.w)):
            return
        norm = math.sqrt(q.x*q.x + q.y*q.y + q.z*q.z + q.w*q.w)
        if norm < 1e-12:
            return
        if msg.header.frame_id == self.child_frame:
            return
        sample = PoseStamped(header=deepcopy(msg.header), pose=deepcopy(pose))
        for field in ('x', 'y', 'z', 'w'):
            setattr(sample.pose.orientation, field, getattr(sample.pose.orientation, field) / norm)
        if self.latest is not None:
            if (sample.header.frame_id != self.latest.header.frame_id or
                    self.stamp(sample) < self.stamp(self.latest)):
                self.history.clear()
                self.consumed_stamp = None
                self.get_logger().info('Frame or timestamp reset: starting a new trajectory')
        self.latest = sample

    def publish_path(self):
        path = Path()
        if self.latest is not None:
            path.header = deepcopy(self.latest.header)
        else:
            path.header.frame_id = 'odom'
            path.header.stamp = self.get_clock().now().to_msg()
        path.poses = list(self.history)
        self.path_pub.publish(path)
        self.dirty = False

    def tick(self):
        if self.latest is not None and self.stamp(self.latest) != self.consumed_stamp:
            sample = self.latest
            self.history.append(sample)
            self.consumed_stamp = self.stamp(sample)
            self.pose_pub.publish(sample)
            transform = TransformStamped()
            transform.header = deepcopy(sample.header)
            transform.child_frame_id = self.child_frame
            transform.transform.translation.x = sample.pose.position.x
            transform.transform.translation.y = sample.pose.position.y
            transform.transform.translation.z = sample.pose.position.z
            transform.transform.rotation = deepcopy(sample.pose.orientation)
            self.tf.sendTransform(transform)
            self.dirty = True
        # Publish the full history at 5 Hz; current pose and TF use --rate.
        now = time.monotonic()
        if self.dirty and now >= self.next_path_publish:
            self.publish_path()
            self.next_path_publish = now + 0.2

    def clear(self, request, response):
        self.history.clear()
        self.publish_path()
        response.success = True
        response.message = 'Trajectory cleared'
        return response


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--input-topic', default='/tidesense/pose')
    parser.add_argument('--input-type', choices=('pose', 'odometry'), default='pose')
    parser.add_argument('--rate', type=float, default=20.0,
                        help='Maximum visualization sample rate (default: 20 Hz)')
    parser.add_argument('--max-points', type=int, default=6000,
                        help='Maximum history length (default: about 5 minutes at 20 Hz)')
    parser.add_argument('--child-frame', default='tidesense_viz_body')
    parser.add_argument('--check', action='store_true', help=argparse.SUPPRESS)
    args, ros_args = parser.parse_known_args()
    if not math.isfinite(args.rate) or not 0 < args.rate <= 200 or args.max_points < 2:
        parser.error('rate must be in (0, 200], max-points must be >= 2')
    if not args.child_frame or args.child_frame.startswith('/'):
        parser.error('child-frame must be a nonempty TF frame without a leading slash')
    if args.check:
        return
    rclpy.init(args=ros_args)
    node = VioPath(args)
    try:
        rclpy.spin(node)
    except (KeyboardInterrupt, ExternalShutdownException):
        pass
    finally:
        node.destroy_node()
        if rclpy.ok():
            rclpy.shutdown()


if __name__ == '__main__':
    main()
