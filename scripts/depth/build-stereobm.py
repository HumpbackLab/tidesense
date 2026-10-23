#!/usr/bin/env python3
"""Build the audited all64 matcher as an isolated object, leaving SDK OpenCV intact."""
import argparse
import hashlib
import json
import shlex
import subprocess
from pathlib import Path

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--compiler', required=True)
p.add_argument('--output', type=Path, required=True)
a = p.parse_args()
sdk = Path(__file__).resolve().parents[2]
source = sdk / 'install/schurvins-build/deps/opencv-3.4.20/modules/calib3d/src/stereobm.cpp'
flags = sdk / 'install/schurvins-gcc11-static/opencv/modules/calib3d/CMakeFiles/opencv_calib3d.dir/flags.make'
generator = sdk / 'scripts/vio-compare/probes/stereo-bm/prepare-opt.py'
digest = hashlib.sha256(source.read_bytes()).hexdigest()
if digest != '26b09c401f21ad067aaaf8c170757eae378c495ddda434c4bad1a47e8616b8c9':
    raise SystemExit('StereoBM source differs from audited OpenCV 3.4.20; review before enabling all64')
a.output.parent.mkdir(parents=True, exist_ok=True)
generated = a.output.with_suffix('.cpp')
subprocess.run(['python3', str(generator), str(source), str(generated)], check=True)
options = {}
for line in flags.read_text().splitlines():
    if line.startswith('CXX_') and ' = ' in line:
        key, value = line.split(' = ', 1)
        options[key] = shlex.split(value)
macros = {'CV_STEREOBM_A53_STRIPE_ROWS': 64, 'CV_STEREOBM_A53_WIDE': 1,
          'CV_STEREOBM_A53_RECOMPUTE': 1}
command = [a.compiler, *options['CXX_DEFINES'], *options['CXX_INCLUDES'], *options['CXX_FLAGS'],
           '-I'+str(source.parent), *[f'-D{k}={v}' for k, v in macros.items()],
           '-c', str(generated), '-o', str(a.output)]
subprocess.run(command, check=True)
a.output.with_suffix('.json').write_text(json.dumps({
    'profile': 'all64', 'source_sha256': digest, 'macros': macros,
    'generated_sha256': hashlib.sha256(generated.read_bytes()).hexdigest(),
    'object_sha256': hashlib.sha256(a.output.read_bytes()).hexdigest(),
    'compile_command': command}, indent=2)+'\n')
