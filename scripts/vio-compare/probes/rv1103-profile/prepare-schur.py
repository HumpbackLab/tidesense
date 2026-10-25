#!/usr/bin/env python3
"""Copy the local ov_SchurVINS replay into an isolated, instrumented experiment."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil
import subprocess
import sys

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('output', type=Path)
parser.add_argument('--reference', type=Path, default=Path('/home/ncer/fpga_openvins'))
args = parser.parse_args()
src = args.output.resolve() / 'src'
if src.exists():
    raise SystemExit(f'Refusing to overwrite {src}')
source = args.reference / 'third_party/ov_SchurVINS'
shutil.copytree(source, src / 'third_party/open_vins', ignore=shutil.ignore_patterns('.git'))
shutil.copytree(args.reference / 'benchmarks/ov_schurvins_replay', src / 'benchmarks/openvins_replay')
shutil.copytree(args.reference / 'benchmarks/replay_common', src / 'benchmarks/replay_common')
cmake = src / 'benchmarks/openvins_replay/CMakeLists.txt'
cmake.write_text(cmake.read_text().replace('../../third_party/ov_SchurVINS', '../../third_party/open_vins'))
subprocess.run([sys.executable, str(Path(__file__).with_name('instrument.py')), str(src), '--schur'], check=True)
config = args.output.resolve() / 'share/openvins/euroc_mav'
shutil.copytree(src / 'third_party/open_vins/config/euroc_mav', config)
yaml = config / 'estimator_config.yaml'
text = yaml.read_text()
for key in ['use_fej', 'calib_cam_extrinsics', 'calib_cam_intrinsics', 'calib_cam_timeoffset']:
    text, count = re.subn(r'^'+key+r': true', key+': false', text, flags=re.M)
    assert count == 1, key
yaml.write_text(text)
manifest = {str(p.relative_to(src)): hashlib.sha256(p.read_bytes()).hexdigest()
            for p in sorted(src.rglob('*')) if p.is_file()}
(args.output.resolve() / 'source-manifest.json').write_text(json.dumps(manifest, indent=2)+'\n')
print(src)
