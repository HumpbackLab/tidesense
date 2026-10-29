#!/usr/bin/env python3
"""Draw the same frame's measured wall-time phases for SD and cached replay."""
import argparse
import csv
from pathlib import Path
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
from matplotlib.patches import Patch

p=argparse.ArgumentParser(description=__doc__);p.add_argument('results',type=Path);a=p.parse_args()
def events(name):
    out=list(csv.DictReader((a.results/(name+'-trace.csv')).open()))
    for e in out:
        for k in ['frame_stamp','begin_ms','end_ms','cpu_ms']:e[k]=float(e[k])
    return out
sd=events('gate0');memory=events('memory')
rows=list(csv.DictReader((a.results/'memory.csv').open()))
lo,hi=float(rows[1002]['timestamp_ns'])*1e-9,float(rows[1038]['timestamp_ns'])*1e-9
common=sorted(set(e['frame_stamp']for e in sd if e['kind']=='tpu') & set(e['frame_stamp']for e in memory if e['kind']=='tpu'))
candidates=[t for t in common if lo<=t<hi]
stamp=max(candidates,key=lambda t:max(e['end_ms']-e['begin_ms']for e in memory if e['frame_stamp']==t and e['kind']=='consume_wait'))
index=min(range(len(rows)),key=lambda i:abs(float(rows[i]['timestamp_ns'])*1e-9-stamp))
fig,axes=plt.subplots(2,1,figsize=(12,6.7),sharex=True,constrained_layout=True)
colors={'backend':'#2974ad','consume_wait':'#e69138','kickoff_wait':'#e69138',
        'preprocess':'#8b64ab','detection':'#c6a3d7','c906':'#318b59','tpu':'#c33740'}
for ax,ev,title in zip(axes,[sd,memory],['Synchronous SD image reads','Same images preloaded in RAM']):
    owner=[e for e in ev if e['frame_stamp']==stamp]
    previous=max(e['frame_stamp']for e in ev if e['kind']=='backend' and e['frame_stamp']<stamp)
    backend=next(e for e in ev if e['kind']=='backend' and e['frame_stamp']==previous)
    relevant=[e for e in owner if e['kind']in ['worker','preprocess','detection','c906','tpu','consume_wait','kickoff_wait']]+[backend]
    origin=min(e['begin_ms']for e in relevant)
    consume=next(e for e in owner if e['kind']=='consume_wait')
    outside=max(0,consume['begin_ms']-backend['end_ms'])
    if outside:ax.broken_barh([(backend['end_ms']-origin,outside)],(2.7,.6),facecolors='#dddddd')
    for e in relevant:
        lane={'backend':3,'consume_wait':3,'kickoff_wait':3,'worker':2,'preprocess':2,'detection':2,'c906':1,'tpu':0}[e['kind']]
        color='#eeeeee'if e['kind']=='worker'else colors[e['kind']]
        ax.broken_barh([(e['begin_ms']-origin,max(.03,e['end_ms']-e['begin_ms']))],(lane-.3,.6),facecolors=color,zorder=0 if e['kind']=='worker'else 1)
    ax.set_yticks([3,2,1,0],['A53 main phases','A53 worker phases','C906 LK execution','TPU calls'])
    ax.set_ylim(-.6,3.7);ax.grid(axis='x',alpha=.25);ax.set_axisbelow(True)
    ax.set_title(f"{title}: frame {index}; main result wait {consume['end_ms']-consume['begin_ms']:.2f} ms; "
                 f"previous backend CPU {backend['cpu_ms']:.2f} ms")
axes[-1].set_xlabel('Elapsed time (ms; aligned separately at worker / kickoff start)')
fig.suptitle('OpenVINS FP64 on SG2002: an example of SD reads hiding frontend wait',fontsize=13)
fig.legend(handles=[Patch(color=colors['backend'],label='Backend phase'),Patch(color=colors['consume_wait'],label='Main wait'),
 Patch(color='#dddddd',label='Main loop outside camera call'),Patch(color=colors['preprocess'],label='Preprocessing'),
 Patch(color=colors['detection'],label='Detection')],loc='lower center',bbox_to_anchor=(.5,-.04),ncol=5)
for suffix in ['png','svg']:fig.savefig(a.results/('timing-timeline.'+suffix),dpi=160,bbox_inches='tight')
print(a.results/'timing-timeline.png')
