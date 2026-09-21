#!/usr/bin/env python3
"""Deploy into a temporary board directory and verify calibration ROS images.
Source the host ROS/Agent environments and set SSHPASS before use.
"""
import argparse
import json
import os
from pathlib import Path
import shlex
import subprocess
import time

p = argparse.ArgumentParser()
p.add_argument('--package', required=True, type=Path)
p.add_argument('--agent', required=True, type=Path)
p.add_argument('--output', required=True, type=Path)
p.add_argument('--mock', type=Path, help='Use this camera fixture instead of hardware')
p.add_argument('--fps', type=float, default=5)
p.add_argument('--agent-verbosity', type=int, default=4)
p.add_argument('--frames', type=int, default=100)
p.add_argument('--restart', action='store_true')
p.add_argument('--imu', action='store_true')
p.add_argument('--board', default='root@192.168.123.99')
p.add_argument('--host-ip', default='192.168.123.2')
a = p.parse_args()
if a.imu and a.restart:
    raise SystemExit('Combined recording must fail on disconnect; use a dedicated failure test')
if 'SSHPASS' not in os.environ:
    raise SystemExit('Set SSHPASS')
a.output.mkdir(parents=True, exist_ok=False)
remote = '/tmp/tidesense-calibration-'+str(time.time_ns())
ssh = ['sshpass', '-e', 'ssh', '-o', 'ConnectTimeout=5', a.board]
scp = ['sshpass', '-e', 'scp', '-O']
env = dict(os.environ, ROS_DOMAIN_ID='24', RMW_IMPLEMENTATION='rmw_cyclonedds_cpp')
processes, logs, events = [], [], []
board = None


def start(command, name):
    log = (a.output/(name+'.log')).open('w')
    logs.append(log)
    proc = subprocess.Popen(command, env=env, stdout=log, stderr=subprocess.STDOUT)
    processes.append(proc)
    events.append({'name': name, 'time': time.monotonic()})
    return proc


def agent(name):
    return start([str(a.agent.resolve()), 'udp4', '-p', '18889', '-v', str(a.agent_verbosity)], name)


try:
    subprocess.run(ssh+['mkdir '+remote], check=True)
    for name in ('schurvins_stereo', 'schurvins_camera', 'fsin.sh', 'prepare-imu.sh'):
        subprocess.run(scp+[str(a.package/'bin'/name), a.board+':'+remote+'/'+name], check=True)
    if a.mock:
        subprocess.run(scp+[str(a.mock), a.board+':'+remote+'/mock'], check=True)
    duration = a.frames/a.fps+12
    cmd = ['python3', str(Path(__file__).with_name('calibration_receiver.py')),
           '--seconds', str(duration), '--output', str(a.output),
           '--ready-file', str(a.output/'ready')]
    if a.mock:
        cmd.append('--mock')
    if a.imu:
        cmd.append('--imu')
    receiver = start(cmd, 'receiver')
    deadline = time.monotonic()+60
    while not (a.output/'ready').exists():
        if receiver.poll() is not None or time.monotonic()>deadline:
            raise RuntimeError('Receiver startup failed')
        time.sleep(.1)
    ag = agent('agent-1')
    variables = ['SCHUR_MICRO_ROS=1', 'SCHUR_MICRO_ROS_AGENT='+a.host_ip,
                 'SCHUR_MICRO_ROS_PORT=18889', 'SCHUR_MICRO_ROS_DOMAIN=24']
    if a.mock:
        variables += ['SCHUR_CAMERA_HELPER='+remote+'/mock', 'SCHUR_CAMERA_MOCK_MODE=calibration']
    args = ['env']+variables+[remote+'/schurvins_stereo', '--calibrate', '/mnt/data/sensor_cfg.ini',
                            '--fps', str(a.fps), '--frames', str(a.frames),
                            '--timestamps', remote+'/stamps.csv']
    if a.imu:
        device = subprocess.check_output(ssh+[remote+'/prepare-imu.sh'], text=True).strip()
        args += ['--imu', device, '--imu-timestamps', remote+'/imu-stamps.csv']
    board = start(ssh+['echo $$ > '+remote+'/pid; exec '+shlex.join(args)], 'board')
    if a.restart:
        time.sleep(8)
        ag.terminate(); ag.wait(timeout=5)
        events.append({'name': 'agent-stop', 'time': time.monotonic()})
        time.sleep(3)
        ag = agent('agent-2')
    assert board.wait(timeout=duration+20) == 0, 'Board failed'
    assert receiver.wait(timeout=30) == 0, 'Image validation failed'
    subprocess.run(scp+[a.board+':'+remote+'/stamps.csv', str(a.output/'stamps.csv')], check=True)
    data = json.loads((a.output/'received.json').read_text())
    if a.imu:
        subprocess.run(scp+[a.board+':'+remote+'/imu-stamps.csv', str(a.output/'imu-stamps.csv')], check=True)
        import csv
        rows = [r for r in csv.reader((a.output/'imu-stamps.csv').read_text().splitlines()) if r and not r[0].startswith('#')]
        samples = {int(r[1]): [float(v) for v in r[2:]] for r in rows}
        image_rows = [r for r in csv.reader((a.output/'stamps.csv').read_text().splitlines()) if r and not r[0].startswith('#')]
        offsets = {int(r[1])-int(r[0]) for r in rows}
        assert len(offsets) == 1 and offsets == {int(r[3])-int(r[1]) for r in image_rows}, 'Different image/IMU clock mapping'
        assert all(m['stamp'] in samples and m['values'] == samples[m['stamp']] for m in data['imu']), 'ROS changed IMU timestamp or SI data'
    if a.restart:
        stop = next(e['time'] for e in events if e['name']=='agent-stop')
        restart = next(e['time'] for e in events if e['name']=='agent-2')
        assert any(x['received']<stop for x in data['pairs']), 'No initial image pairs'
        assert any(x['received']>restart+2 for x in data['pairs']), 'No image reconnect'
    print('PASS:', len(data['pairs']), 'pairs;', data['pair_rate_hz'], 'Hz')
finally:
    # TERM the exact process started by this test, allowing helper/FSIN cleanup.
    if board is not None and board.poll() is None:
        subprocess.run(ssh+['if test -f '+remote+'/pid; then kill -TERM $(cat '+remote+'/pid) 2>/dev/null || true; fi'],
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=10)
    for proc in reversed(processes):
        if proc.poll() is None:
            proc.terminate()
            try: proc.wait(timeout=10)
            except subprocess.TimeoutExpired:
                proc.kill()
                try: proc.wait(timeout=3)
                except subprocess.TimeoutExpired:
                    print('Process remains blocked in host kernel:', proc.pid, flush=True)
    for log in logs:
        log.close()
    (a.output/'events.json').write_text(json.dumps(events, indent=2))
    subprocess.run(ssh+['rm -rf '+remote], timeout=10, check=True)
