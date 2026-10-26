#!/usr/bin/env python3
"""Run isolated, camera-free transport benchmarks. Source host-env.sh first."""
import argparse,json,os,shlex,subprocess,tarfile,time,uuid
from pathlib import Path
import paramiko
p=argparse.ArgumentParser();p.add_argument('--password-env',default='TIDE_DEVICE_PASSWORD');p.add_argument('--seconds',type=int,default=30);p.add_argument('--smoke',action='store_true');a=p.parse_args()
root=Path(__file__).resolve().parents[3];here=Path(__file__).resolve().parent
out=root/'install/native-dds'/('results-'+time.strftime('%Y%m%d-%H%M%S'));out.mkdir()
remote='/mnt/data/tide-dds-bench-'+uuid.uuid4().hex[:8]
def config(address,peers):
 return '<CycloneDDS><Domain><General><Interfaces>'+''.join('<NetworkInterface address="'+x+'"/>' for x in address)+'</Interfaces><AllowMulticast>false</AllowMulticast><MaxMessageSize>1400B</MaxMessageSize><FragmentSize>1200B</FragmentSize></General><Discovery><ParticipantIndex>auto</ParticipantIndex><MaxAutoParticipantIndex>10</MaxAutoParticipantIndex><Peers>'+''.join('<Peer Address="'+x+'"/>' for x in peers)+'</Peers></Discovery></Domain></CycloneDDS>'
(out/'host.xml').write_text(config(['192.168.123.2','127.0.0.1'],['192.168.123.99','127.0.0.1']))
(out/'board.xml').write_text(config(['192.168.123.99'],['192.168.123.2']))
c=paramiko.SSHClient();c.load_host_keys(str(root/'install/tide-configurator-data/known_hosts'))
c.connect('192.168.123.99',username='root',password=os.environ[a.password_env],look_for_keys=False,allow_agent=False,timeout=8)
def execute(command):
 _,stdout,stderr=c.exec_command(command);text=stdout.read().decode();error=stderr.read().decode();code=stdout.channel.recv_exit_status()
 if code:raise RuntimeError((command,code,text,error))
 return text+error
processes=[];sftp=None;created=False
try:
 execute('mkdir '+shlex.quote(remote));created=True
 package=root/'install/native-dds/package';archive=out/'runtime.tar'
 with tarfile.open(archive,'w') as tf:
  for f in package.iterdir():tf.add(f,arcname=f.name)
  tf.add(out/'board.xml',arcname='board.xml')
 sftp=c.open_sftp();sftp.put(str(archive),remote+'/runtime.tar');sftp.close();sftp=None
 execute('tar -xf '+remote+'/runtime.tar -C '+remote)
 execute('chmod +x '+remote+'/lib/ld-linux-aarch64.so.1 '+remote+'/native_bench '+remote+'/micro_bench')
 (out/'board.txt').write_text(execute('uname -a; cat /sys/kernel/debug/clk/clk_summary | sed -n "/clk_a53/p"; cat /proc/meminfo | head -n 4'))
 runs=[('micro',200,True),('native',200,True)] if a.smoke else [
  ('off',200,False),('micro',200,True),('native',200,True),
  ('native',200,True),('micro',200,True),
  ('off',20,False),('micro',20,True),('native',20,True),
  ('native',200,False),('micro',200,False)]
 results=[]
 for n,(mode,hz,subscribe) in enumerate(runs):
  label=f'{n:02d}-{mode}-{hz}-'+('sub' if subscribe else 'none')
  print('START',label,flush=True);files=[];owned=[]
  env=dict(os.environ,ROS_DOMAIN_ID='37',CYCLONEDDS_URI='file://'+str(out/'host.xml'))
  try:
   if mode=='micro':
    agentlog=(out/(label+'-agent.log')).open('w');files.append(agentlog)
    agent=subprocess.Popen([str(root/'install/micro-ros/agent-install/lib/micro_ros_agent/micro_ros_agent'),'udp4','-p','18889','-v','0'],env=dict(env,FASTRTPS_DEFAULT_PROFILES_FILE=str(root/'scripts/schurvins/micro_ros/calibration-agent.xml')),stdout=agentlog,stderr=subprocess.STDOUT);owned.append(agent);processes.append(agent)
    time.sleep(.5)
   received=out/(label+'-received.json')
   if subscribe:
    log=(out/(label+'-receiver.log')).open('w');files.append(log)
    reader=subprocess.Popen(['/usr/bin/python3',str(here/'receiver.py'),'--hz',str(hz),'--seconds',str(a.seconds),'--output',str(received)],env=env,stdout=log,stderr=subprocess.STDOUT);owned.append(reader);processes.append(reader)
    deadline=time.monotonic()+15
    while not received.with_suffix('.ready').exists():
     if reader.poll() is not None or time.monotonic()>deadline:raise RuntimeError('Receiver did not start: '+label)
     time.sleep(.1)
   variables={'SCHUR_MICRO_ROS':'0' if mode=='off' else '1','SCHUR_MICRO_ROS_AGENT':'192.168.123.2','SCHUR_MICRO_ROS_PORT':'18889','SCHUR_MICRO_ROS_DOMAIN':'37','ROS_DOMAIN_ID':'37','CYCLONEDDS_URI':'file://'+remote+'/board.xml','AMENT_PREFIX_PATH':remote}
   command='env '+shlex.join([k+'='+v for k,v in variables.items()])+' '
   if mode=='native':command+=shlex.join([remote+'/lib/ld-linux-aarch64.so.1','--library-path',remote+'/lib',remote+'/native_bench',str(hz),str(a.seconds)])
   else:command+=shlex.join([remote+'/micro_bench',str(hz),str(a.seconds)])
   stdout=execute(command)
   (out/(label+'-board.log')).write_text(stdout)
   lines=[line for line in stdout.splitlines() if line.startswith('{')]
   assert len(lines)==1,stdout
   result=dict(mode=mode,hz=hz,subscribed=subscribe,measurement=json.loads(lines[0]))
   if subscribe:
    assert reader.wait(timeout=15)==0,label
    result['receiver']=json.loads(received.read_text())
    assert not result['receiver']['errors'] and result['receiver']['missing']==0 and result['receiver']['duplicates']==0,result
   results.append(result);(out/'results.json').write_text(json.dumps(results,indent=2));print('RESULT',json.dumps(result),flush=True)
  finally:
   for proc in reversed(owned):
    if proc.poll() is None:proc.terminate()
   for proc in owned:
    try:proc.wait(timeout=5)
    except subprocess.TimeoutExpired:proc.kill();proc.wait()
   for f in files:f.close()
 print('OUTPUT',out,flush=True)
finally:
 if sftp:sftp.close()
 # Remove only files uploaded by this run; original datasets and services are untouched.
 if created:
  def remove_tree(sftp,path):
   import stat
   for entry in sftp.listdir_attr(path):
    child=path+'/'+entry.filename
    if stat.S_ISDIR(entry.st_mode):remove_tree(sftp,child)
    else:sftp.remove(child)
   sftp.rmdir(path)
  try:
   with c.open_sftp() as sftp:remove_tree(sftp,remote)
  except Exception as exc:print('Cleanup:',exc,flush=True)
 c.close()
