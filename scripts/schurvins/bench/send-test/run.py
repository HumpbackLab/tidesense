#!/usr/bin/env python3
"""Sequential on-board runs with a real host UDP receiver. SSHPASS required."""
import os, pathlib, subprocess, socket, struct, threading, time, json, csv, hashlib
here=pathlib.Path(__file__).resolve().parent
tag=time.strftime('%Y%m%dT%H%M%S',time.gmtime())
out=here/('results-'+tag); out.mkdir()
remote='/tmp/schur-send-'+tag
board='root@192.168.123.99'
ssh=['sshpass','-e','ssh','-o','StrictHostKeyChecking=no','-o','UserKnownHostsFile=/dev/null','-o','ConnectTimeout=5',board]
scp=['sshpass','-e','scp','-O','-o','StrictHostKeyChecking=no','-o','UserKnownHostsFile=/dev/null']
def command(cmd):
    return subprocess.run(ssh+[cmd],check=True,capture_output=True,text=True,timeout=180).stdout
class Receiver:
    def __init__(self):
        self.sock=socket.socket(socket.AF_INET,socket.SOCK_DGRAM)
        self.sock.setsockopt(socket.SOL_SOCKET,socket.SO_RCVBUF,4*1024*1024)
        self.sock.bind(('192.168.123.2',39281)); self.sock.settimeout(.2)
        self.stop=False; self.messages={}; self.packets=0; self.bytes=0; self.bad=0
        self.shapes={}; self.first=None; self.last=None
        self.thread=threading.Thread(target=self.run); self.thread.start()
    def run(self):
        while not self.stop:
            try: data,addr=self.sock.recvfrom(65535)
            except socket.timeout: continue
            if addr[0]!='192.168.123.99' or len(data)<16:
                self.bad+=1; continue
            magic,ident,part,total=struct.unpack_from('<IIII',data)
            if magic!=0x54534431 or part>=total:
                self.bad+=1; continue
            now=time.monotonic(); self.first=self.first or now; self.last=now
            self.packets+=1; self.bytes+=len(data)
            entry=self.messages.setdefault(ident,[total,set()]); entry[1].add(part)
            if part==0 and len(data)>=88:
                self.shapes[ident]=struct.unpack_from('<II',data,80)
    def close(self):
        time.sleep(.3); self.stop=True; self.thread.join(); self.sock.close()
        return dict(packets=self.packets,bytes=self.bytes,bad=self.bad,
                    messages=len(self.messages),complete=sum(len(v[1])==v[0] for v in self.messages.values()),
                    missing_fragments=sum(v[0]-len(v[1]) for v in self.messages.values()),
                    image_messages=sum(w>0 for w,h in self.shapes.values()),
                    image_shapes=list(set(self.shapes.values())),
                    receive_span_s=(self.last-self.first) if self.last else 0)
command('mkdir '+remote)
subprocess.run(scp+[str(here/'build/schurvins_send_test'),board+':'+remote+'/runner'],check=True,capture_output=True)
(out/'setup.txt').write_text(command('cat /proc/meminfo; cat /proc/mounts; ps; cat /sys/kernel/debug/clk/clk_a53/clk_rate'))
order=['base','pose','image2','image5','image5','image2','pose','base']
print('RESULTS',out,flush=True)
for index,mode in enumerate(order):
    name=f'{index:02}-{mode}'
    # Never launch into an existing algorithm benchmark.
    procs=command('ps')
    if any(x in procs for x in ['schurvins_stereo','schurvins_send_test','./runner ']):
        raise RuntimeError('Another VIO process is active; refusing concurrent test')
    before=command('cat /sys/kernel/debug/clk/clk_a53/clk_rate; cat /sys/class/thermal/thermal_zone0/temp; cat /proc/stat')
    receiver=Receiver()
    env='' if mode=='base' else 'SCHUR_SEND_HOST=192.168.123.2 SCHUR_SEND_IMAGE_HZ='+{'pose':'0','image2':'2','image5':'5'}[mode]+' '
    dataset='/mnt/data/schurvins_a53/schurvins_a53_pgm25s/data/EuRoC_V1_01_replay'
    cmd=f'cd {remote} && {env}SCHUR_TIMING_CSV=vio.csv SCHUR_SEND_TIMING=send.csv ./runner /mnt/data/schur-opt-test/calib.yaml /mnt/data/schur-opt-test/stereo.csv {dataset}/mav0/imu0/data.csv poses.csv > run.log 2>&1'
    print('START',name,flush=True); start=time.monotonic()
    try: command(cmd)
    finally: received=receiver.close()
    elapsed=time.monotonic()-start
    for file in ['vio.csv','send.csv','poses.csv','run.log']:
        subprocess.run(scp+[board+':'+remote+'/'+file,str(out/(name+'-'+file))],check=True,capture_output=True)
    after=command('cat /sys/kernel/debug/clk/clk_a53/clk_rate; cat /sys/class/thermal/thermal_zone0/temp; cat /proc/stat')
    (out/(name+'-environment.txt')).write_text(before+'\nAFTER\n'+after)
    (out/(name+'-receiver.json')).write_text(json.dumps(received,indent=2))
    print('DONE',name,'elapsed',round(elapsed,2),'received',received,flush=True)
command(f'rm {remote}/runner {remote}/vio.csv {remote}/send.csv {remote}/poses.csv {remote}/run.log && rmdir {remote}')
print('COMPLETE',out,flush=True)
