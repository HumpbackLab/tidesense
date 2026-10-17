#!/usr/bin/env python3
"""Rebuild SDK sample glue in scratch space and wrap stream completion."""
import argparse
import json
from pathlib import Path
import shlex
import subprocess

ROOT = Path(__file__).resolve().parents[4]
HERE = Path(__file__).resolve().parent
OUT = ROOT / 'install/venc-mv-device-20261017'
SAMPLE = ROOT / 'cvi_mpi/sample/venc'
PREFIX = ROOT / 'host-tools/gcc/gcc-linaro-6.3.1-2017.05-x86_64_aarch64-linux-gnu/bin/aarch64-linux-gnu-'

def run(command):
    print(shlex.join(map(str, command)), flush=True)
    subprocess.run(list(map(str, command)), cwd=SAMPLE, check=True)

if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--out', type=Path, default=OUT)
    OUT = parser.parse_args().out.resolve()
    OUT.mkdir(parents=True, exist_ok=True)
    dry = subprocess.check_output(['make', '-n', '-B', 'sample_venc', 'CROSS_COMPILE=' + str(PREFIX)], cwd=SAMPLE, text=True)
    (OUT / 'make-dry-run.txt').write_text(dry)
    commands = [shlex.split(s) for s in dry.splitlines() if s.startswith(str(PREFIX))]
    # Old sample objects predate PIC_CUSTOMIZE. Recompile every sample object
    # with the current headers, without overwriting the SDK's original objects.
    replacement = {}
    for command in commands:
        if '-c' not in command:
            continue
        old = command[command.index('-o') + 1]
        new = OUT / (str(len(replacement)) + '-' + Path(old).name)
        replacement[old] = str(new)
        command[command.index('-o') + 1] = str(new)
        original_source = Path(command[-1])
        if original_source.name == 'sample_venc_lib.c':
            text = original_source.read_text()
            needle = 'static CVI_S32 cviReadSrcFrame(VIDEO_FRAME_S *pstVFrame, FILE *fp)\n{'
            assert text.count(needle) == 1
            text = text.replace(needle, needle + '''
  extern int venc_mv_read_input(VIDEO_FRAME_S *);
  if (getenv("VENC_MV_FRAME_LIST")) return venc_mv_read_input(pstVFrame);
''')
            copy = OUT / 'sample_venc_lib.c'
            copy.write_text(text)
            command[-1] = str(copy)
        run(command)
    wrapper = OUT / 'device_probe.o'
    run([str(PREFIX) + 'gcc', '-O2', '-g', '-D__CV181X__', '-I' + str(ROOT / 'cvi_mpi/include'),
         '-c', HERE / 'device_probe.c', '-o', wrapper])
    link = next(c for c in commands if '-c' not in c)
    link = [replacement.get(arg, arg) for arg in link]
    link[link.index('-o') + 1] = str(OUT / 'sample_venc_probe')
    cache = OUT / 'device_cache.o'
    run([str(PREFIX) + 'gcc', '-O2', '-g', '-D__CV181X__', '-I' + str(ROOT / 'cvi_mpi/include'),
         '-c', HERE / 'device_cache.c', '-o', cache])
    link += [str(wrapper), str(cache), '-Wl,--wrap=CVI_VENC_GetStream', '-Wl,--wrap=CVI_VENC_SendFrame', '-Wl,--wrap=system']
    run(link)
    run([str(PREFIX) + 'strip', OUT / 'sample_venc_probe'])
    run([str(PREFIX) + 'gcc', '-O2', '-static', '-I' + str(ROOT / 'cvi_mpi/include'),
         HERE / 'cleanup_idle.c', '-L' + str(ROOT / 'cvi_mpi/lib'), '-lsys', '-lmisc', '-lpthread',
         '-lm', '-latomic', '-o', OUT / 'cleanup_idle'])
    (OUT / 'build-command.json').write_text(json.dumps(link, indent=2) + '\n')
