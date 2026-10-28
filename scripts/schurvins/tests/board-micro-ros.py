#!/usr/bin/env python3
"""Run the camera-free transport fixture on TideSense; SSHPASS must be set.
Source ROS 2 and the built micro-ROS Agent before invoking this script.
"""
import argparse
import json
import os
from pathlib import Path
import shlex
import subprocess
import time

p = argparse.ArgumentParser()
p.add_argument('--binary', required=True, type=Path)
p.add_argument('--agent', required=True, type=Path)
p.add_argument('--board', default='root@192.168.123.99')
p.add_argument('--host-ip', default='192.168.123.2')
p.add_argument('--domain', type=int, default=23)
p.add_argument('--rmw', default='rmw_cyclonedds_cpp')
p.add_argument('--agent-verbosity', type=int, default=4)
p.add_argument('--hold-seconds', type=int, default=12)
p.add_argument('--output', required=True, type=Path)
a = p.parse_args()
if 'SSHPASS' not in os.environ:
    raise SystemExit('Set SSHPASS for the test board')
a.output.mkdir(parents=True, exist_ok=False)
remote = '/tmp/schur-micro-ros-' + str(int(time.time()))
(a.output/'remote-directory.txt').write_text(remote + '\n')
ssh = ['sshpass', '-e', 'ssh', '-o', 'ConnectTimeout=5', a.board]
scp = ['sshpass', '-e', 'scp', '-O']
env = dict(os.environ, ROS_DOMAIN_ID=str(a.domain), RMW_IMPLEMENTATION=a.rmw)
processes, files, events = [], [], []
agent = None

def event(name):
    events.append({'name': name, 'time': time.monotonic()})

def start_agent(index):
    log = (a.output / f'agent-{index}.log').open('w')
    files.append(log)
    proc = subprocess.Popen([str(a.agent.resolve()), 'udp4', '-p', '18888', '-v', str(a.agent_verbosity)],
                            env=env, stdout=log, stderr=subprocess.STDOUT)
    processes.append(proc)
    event(f'agent-{index}-start')
    return proc

try:
    subprocess.run(ssh + ['mkdir ' + remote], check=True)
    subprocess.run(scp + [str(a.binary), a.board + ':' + remote + '/fixture'], check=True)
    receiver_log = (a.output / 'receiver.log').open('w')
    files.append(receiver_log)
    receiver = subprocess.Popen(['python3', str(Path(__file__).with_name('micro_ros_receiver.py')),
                                 '--seconds', str(a.hold_seconds + 31), '--output', str(a.output/'received.json'),
                                 '--ready-file', str(a.output/'receiver-ready')],
                                env=env, stdout=receiver_log, stderr=subprocess.STDOUT)
    processes.append(receiver)
    # Host ROS imports/discovery setup can be slow on mounted filesystems. Do
    # not spend the initial-connection test window before the reader exists.
    deadline = time.monotonic() + 60
    while not (a.output/'receiver-ready').exists():
        if receiver.poll() is not None or time.monotonic() > deadline:
            raise RuntimeError('ROS receiver did not become ready')
        time.sleep(.1)
    event('receiver-ready')
    fixture_log = (a.output/'fixture.log').open('w')
    files.append(fixture_log)
    cmd = (f'SCHUR_MICRO_ROS=1 SCHUR_MICRO_ROS_AGENT={shlex.quote(a.host_ip)} '
           f'SCHUR_MICRO_ROS_PORT=18888 SCHUR_MICRO_ROS_DOMAIN={a.domain} {remote}/fixture {(a.hold_seconds + 28)*10}')
    fixture = subprocess.Popen(ssh + [cmd], stdout=fixture_log, stderr=subprocess.STDOUT)
    processes.append(fixture); event('fixture-start')
    time.sleep(4)
    agent = start_agent(1)
    time.sleep(a.hold_seconds)
    agent.terminate(); agent.wait(timeout=5); event('agent-1-stop')
    time.sleep(4)
    agent = start_agent(2)
    assert fixture.wait(timeout=30) == 0, 'board fixture failed'
    event('fixture-finished')
    assert receiver.wait(timeout=10) == 0, 'receiver assertions failed'
    data = json.loads((a.output/'received.json').read_text())
    stopped = next(e['time'] for e in events if e['name'] == 'agent-1-stop')
    restarted = next(e['time'] for e in events if e['name'] == 'agent-2-start')
    assert any(x['received'] < stopped for x in data['poses']), 'no initial connection'
    assert any(x['received'] > restarted + 2 for x in data['poses']), 'no reconnect'
    print('BOARD MICRO-ROS PASS: late Agent, valid messages, tracking loss, Agent restart')
finally:
    for proc in reversed(processes):
        if proc.poll() is None:
            proc.terminate()
            try: proc.wait(timeout=5)
            except subprocess.TimeoutExpired: proc.kill(); proc.wait()
    for f in files: f.close()
    (a.output/'events.json').write_text(json.dumps(events, indent=2))
    # Logs and the exact fixture remain on the host. Remove only this test's
    # uploaded file, also after failure, to avoid filling the board's small tmpfs.
    subprocess.run(ssh + [f'rm -f {remote}/fixture && rmdir {remote}'],
                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=10)
