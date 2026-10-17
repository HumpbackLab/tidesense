#!/usr/bin/env python3
"""Resume validated HTTP-range downloads for the official UZH-FPV suite."""
import argparse
from concurrent.futures import ThreadPoolExecutor, as_completed
import hashlib
import json
import os
from pathlib import Path
import threading
import time
import zipfile

import requests


def atomic_json(path, value):
    temporary = path.with_suffix(path.suffix + '.tmp')
    temporary.write_text(json.dumps(value, indent=2) + '\n')
    temporary.replace(path)


def digest(path):
    sha = hashlib.sha256()
    with path.open('rb') as stream:
        for data in iter(lambda: stream.read(8 * 1024 * 1024), b''):
            sha.update(data)
    return sha.hexdigest()


def request_range(url, lo, hi):
    last = None
    for attempt in range(10):
        try:
            response = requests.get(url, headers={'Range': f'bytes={lo}-{hi}'}, timeout=(20, 90))
            response.raise_for_status()
            expected = f'bytes {lo}-{hi}/'
            if response.status_code != 206 or not response.headers.get('Content-Range', '').startswith(expected):
                raise ValueError(f'Invalid range response: {response.status_code}, {response.headers.get("Content-Range")}')
            if len(response.content) != hi - lo + 1:
                raise ValueError('Short HTTP range')
            return response
        except (requests.RequestException, ValueError) as error:
            last = error
            time.sleep(min(1 + attempt, 8))
    raise RuntimeError(f'{url}: {last}')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--manifest', type=Path, default=Path(__file__).with_name('uzh-sequences.json'))
    parser.add_argument('--cache', type=Path, required=True)
    parser.add_argument('--workers', type=int, default=16)
    args = parser.parse_args()
    args.cache.mkdir(parents=True, exist_ok=True)
    manifest = json.loads(args.manifest.read_text())
    items = sorted(manifest['sequences'], key=lambda x: (not x['public_gt'], x['name']))
    items += [dict(name=Path(url).stem, url=url) for url in manifest['calibrations']]
    lock = threading.Lock()
    states = {}
    tasks = []
    started = time.monotonic()

    def prepare(item):
        name, url = item['name'], item['url']
        target = args.cache / (name + '.zip')
        marker = args.cache / (name + '.download.json')
        if target.exists():
            with zipfile.ZipFile(target) as archive:
                archive.infolist()
            if not marker.exists():
                atomic_json(marker, dict(url=url, size=target.stat().st_size, sha256=digest(target), complete=True))
            return None
        response = request_range(url, 0, 0)
        size = int(response.headers['Content-Range'].split('/')[-1])
        temporary = args.cache / (name + '.zip.part')
        resume = args.cache / (name + '.resume.json')
        saved = json.loads(resume.read_text()) if resume.exists() else {}
        completed = set(saved.get('completed', [])) if saved.get('size') == size and temporary.exists() else set()
        fd = os.open(temporary, os.O_CREAT | os.O_RDWR, 0o644)
        os.ftruncate(fd, size)
        block = 16 * 1024 * 1024
        state = dict(name=name, url=url, size=size, target=target, temporary=temporary,
                     resume=resume, completed=completed, fd=fd, block=block,
                     blocks=(size + block - 1) // block)
        return state

    with ThreadPoolExecutor(max_workers=6) as pool:
        for future in as_completed([pool.submit(prepare, item) for item in items]):
            state = future.result()
            if state:
                states[state['name']] = state
                for i in range(state['blocks']):
                    if i not in state['completed']:
                        tasks.append((state, i))
    # Finish one sequence at a time in queue order, while downloading its chunks concurrently.
    priority = {item['name']: i for i, item in enumerate(items)}
    tasks.sort(key=lambda value: (priority[value[0]['name']], value[1]))
    print(f'{len(states)} archives pending, {sum(v["size"] for v in states.values()) / 1e9:.2f} GB', flush=True)

    def fetch(task):
        state, index = task
        lo = index * state['block']
        hi = min(state['size'], lo + state['block']) - 1
        data = request_range(state['url'], lo, hi).content
        view = memoryview(data)
        offset = lo
        while view:
            written = os.pwrite(state['fd'], view, offset)
            if written <= 0:
                raise OSError('Short write')
            offset += written
            view = view[written:]
        with lock:
            state['completed'].add(index)
            atomic_json(state['resume'], dict(url=state['url'], size=state['size'], completed=sorted(state['completed'])))
        return state['name']

    with ThreadPoolExecutor(max_workers=args.workers) as pool:
        for n, future in enumerate(as_completed([pool.submit(fetch, task) for task in tasks]), 1):
            name = future.result()
            state = states[name]
            with lock:
                if len(state['completed']) == state['blocks'] and not state.get('finished'):
                    os.fsync(state['fd'])
                    os.close(state['fd'])
                    with zipfile.ZipFile(state['temporary']) as archive:
                        archive.infolist()
                    state['temporary'].replace(state['target'])
                    atomic_json(args.cache / (name + '.download.json'), dict(url=state['url'], size=state['size'], sha256=digest(state['target']), complete=True))
                    state['finished'] = True
                    print(f'COMPLETE {name} ({state["size"] / 1e6:.1f} MB), elapsed {time.monotonic()-started:.0f}s', flush=True)
            if n % 20 == 0:
                print(f'chunks {n}/{len(tasks)}, elapsed {time.monotonic()-started:.0f}s', flush=True)
    for state in states.values():
        if not state.get('finished') and len(state['completed']) == state['blocks']:
            os.close(state['fd'])
            state['temporary'].replace(state['target'])
            atomic_json(args.cache / (state['name'] + '.download.json'), dict(url=state['url'], size=state['size'], sha256=digest(state['target']), complete=True))


if __name__ == '__main__':
    main()
