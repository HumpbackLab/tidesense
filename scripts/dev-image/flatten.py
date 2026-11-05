#!/usr/bin/env python3
"""Flatten Kalibr through a filesystem export, preserving its runtime config.

Export/import streams file contents instead of merging all historical layer diffs
in memory. The resulting single layer avoids VFS ancestor copies inside the SDK.
"""
import json
from pathlib import Path
import subprocess
import sys


def flatten(source, destination, archive):
    config = json.loads(subprocess.check_output(['podman', 'image', 'inspect', source]))[0]['Config']
    command = ['podman', 'import']
    for key in ('Entrypoint', 'Cmd'):
        if config.get(key) is not None:
            command += ['--change', f'{key.upper()} {json.dumps(config[key])}']
    for entry in config.get('Env') or []:
        key, value = entry.split('=', 1)
        command += ['--change', f'ENV {key}={json.dumps(value)}']
    for key, value in (config.get('Labels') or {}).items():
        command += ['--change', f'LABEL {json.dumps(key)}={json.dumps(value)}']
    for key, instruction in (('WorkingDir', 'WORKDIR'), ('User', 'USER'), ('StopSignal', 'STOPSIGNAL')):
        if config.get(key):
            command += ['--change', f'{instruction} {config[key]}']
    container = subprocess.check_output(['podman', 'create', source], text=True).strip()
    try:
        subprocess.run(['podman', 'export', '--output', str(archive), container], check=True)
    finally:
        subprocess.run(['podman', 'rm', container], check=True)
    try:
        subprocess.run(command + [str(archive), destination], check=True)
    finally:
        archive.unlink(missing_ok=True)


if __name__ == '__main__':
    flatten(sys.argv[1], sys.argv[2], Path(sys.argv[3]))
