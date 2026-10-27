#!/usr/bin/env python3
"""Verify the benchmark's actual ROS 2 payload and measured-window delivery."""
import argparse,json,time
from pathlib import Path
import rclpy
from rclpy.qos import QoSProfile,ReliabilityPolicy,HistoryPolicy
from geometry_msgs.msg import PoseStamped
from std_msgs.msg import Bool
p=argparse.ArgumentParser();p.add_argument('--hz',type=int,required=True);p.add_argument('--seconds',type=int,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args()
rclpy.init();node=rclpy.create_node('tidesense_dds_benchmark_receiver')
qos=QoSProfile(depth=64,reliability=ReliabilityPolicy.BEST_EFFORT,history=HistoryPolicy.KEEP_LAST)
seen=set();duplicates=0;errors=[];arrivals=[];tracking=0
start=time.monotonic();step=10**9//a.hz;first=a.hz*5;last=first+a.hz*a.seconds

def pose(msg):
 global duplicates
 stamp=msg.header.stamp.sec*10**9+msg.header.stamp.nanosec
 seq,residue=divmod(stamp-1700000000000000000,step)
 p,q=msg.pose.position,msg.pose.orientation
 if residue or msg.header.frame_id!='odom' or (p.x,p.y,p.z,q.x,q.y,q.z,q.w)!=(1.25,-2.5,3.75,0.,0.,0.,1.):errors.append('Invalid payload')
 if first<=seq<last:
  if seq in seen:duplicates+=1
  seen.add(seq);arrivals.append(time.monotonic())
def track(msg):
 global tracking
 tracking+=1
 if not msg.data:errors.append('Unexpected tracking=false')
node.create_subscription(PoseStamped,'/tidesense/pose',pose,qos)
node.create_subscription(Bool,'/tidesense/tracking',track,qos)
a.output.with_suffix('.ready').touch()
try:
 while time.monotonic()-start<a.seconds+11:rclpy.spin_once(node,timeout_sec=.1)
 result=dict(expected=a.hz*a.seconds,received=len(seen),missing=a.hz*a.seconds-len(seen),duplicates=duplicates,errors=errors,tracking_total=tracking,received_hz=(len(arrivals)-1)/(arrivals[-1]-arrivals[0]) if len(arrivals)>1 else 0,max_receive_gap_ms=max((b-a for a,b in zip(arrivals,arrivals[1:])),default=0)*1000)
 a.output.write_text(json.dumps(result,indent=2));print(json.dumps(result),flush=True)
finally:node.destroy_node();rclpy.shutdown()
