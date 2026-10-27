"""Run both Kalibr solvers directly against one read-only bag."""
import json
import os
from pathlib import Path
import subprocess

LENSES = {'wide': 'pinhole-radtan', 'fisheye': 'pinhole-equi'}
DEFAULT_IMAGE = 'localhost/tidesense-calibration:noetic'
ENV_CHECK = '/usr/local/lib/tidesense-calibration-env.py'
RESULTS = {
    'cameras': ('dataset-camchain.yaml', 'dataset-results-cam.txt', 'dataset-report-cam.pdf'),
    'imucam': ('dataset-camchain-imucam.yaml', 'dataset-imu.yaml',
               'dataset-results-imucam.txt', 'dataset-report-imucam.pdf'),
}


class Runtime:
    def __init__(self):
        self.image = os.environ.get('KALIBR_IMAGE', DEFAULT_IMAGE)
        self.image_info = None

    def preflight(self):
        build = Path(__file__).resolve().parents[1] / 'environment/build.sh'
        try:
            self.image_info = json.loads(subprocess.check_output(
                ['podman', 'image', 'inspect', self.image], text=True))
        except (OSError, subprocess.CalledProcessError) as exc:
            raise RuntimeError(f'Kalibr image unavailable; build first: {build}') from exc
        # Pin this execution to the inspected image even if its tag is changed later.
        self.image = self.image_info[0]['Id']
        subprocess.run(['podman', 'run', '--rm', '--init', '--network=none',
                        '--cap-drop=all', '--security-opt=no-new-privileges',
                        self.image, 'python3', ENV_CHECK, '--check'], check=True)

    def solve(self, output, stage, lens):
        command = ['podman', 'run', '--rm', '--init', '--network=none', '--cap-drop=all',
                   '--security-opt=no-new-privileges', '--env', 'PYTHONUNBUFFERED=1',
                   '--env', 'OMP_NUM_THREADS=2', '--env', 'OPENBLAS_NUM_THREADS=1',
                   '--volume', f'{output}:/output:rw',
                   '--volume', f'{output / "dataset.bag"}:/output/dataset.bag:ro',
                   '--workdir', '/output', self.image, 'xvfb-run', '-a', 'rosrun', 'kalibr']
        if stage == 'cameras':
            command += ['kalibr_calibrate_cameras', '--bag', '/output/dataset.bag',
                        '--target', '/output/target.yaml', '--topics', '/cam0/image_raw', '/cam1/image_raw',
                        '--models', LENSES[lens], LENSES[lens], '--dont-show-report']
        elif stage == 'imucam':
            command += ['kalibr_calibrate_imu_camera', '--bag', '/output/dataset.bag',
                        '--cams', '/output/dataset-camchain.yaml', '--imu', '/output/imu.yaml',
                        '--target', '/output/target.yaml', '--imu-models', 'scale-misalignment',
                        '--max-iter', '30', '--dont-show-report']
        else:
            raise ValueError(f'Unknown calibration stage: {stage}')
        (output / f'{stage}-command.json').write_text(json.dumps(command, indent=2) + '\n')
        log = output / f'{stage}.log'
        print(f'  Log: {log}', flush=True)
        with log.open('w') as stream:
            result = subprocess.run(command, stdout=stream, stderr=subprocess.STDOUT)
        if result.returncode:
            raise RuntimeError(f'{stage} exited {result.returncode}; inspect {log}')
        for name in RESULTS[stage]:
            path = output / name
            if not path.is_file() or path.stat().st_size == 0:
                raise RuntimeError(f'Kalibr did not produce {name}; inspect {log}')
