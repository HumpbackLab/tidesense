#!/usr/bin/env python3
"""Known-motion monochrome YUV420p fixtures for hardware MV validation."""
import json
from pathlib import Path
import sys
sys.path.insert(0, '/usr/lib/python3/dist-packages')
import numpy as np

ROOT = Path(__file__).resolve().parents[4]
OUT = ROOT / 'install/venc-mv-device-20261017'
OUT.mkdir(exist_ok=True)
rng = np.random.RandomState(9281)
a = rng.randint(0, 256, (400, 640), dtype=np.uint8)
a = ((a.astype(np.uint16) + np.roll(a, 1, 0) + np.roll(a, 1, 1) + np.roll(a, 1, (0, 1))) // 4).astype(np.uint8)
steps = [(0, 0), (0, 0), (0, 0), (8, 4), (-6, 2), (3, -5), (-4, -3),
         (0, 0), (7, -2), (-5, 1), (1, -4)]
metadata = {}
for name, dx, dy, frames in [('shift-8-4', 8, 4, 16), ('shift-neg6-2', -6, 2, 12),
                              ('quarter', 1.5, .75, 12), ('split', 0, 0, 12),
                              ('quadrants', 0, 0, 12), ('dynamic', 0, 0, 12)]:
    expected = []
    offset_x = offset_y = 0
    with (OUT / (name + '.yuv')).open('wb') as out:
        for i in range(frames):
            k = max(0, i - 3)
            if name in ('split', 'quadrants'):
                if name == 'split':
                    image = np.concatenate([np.roll(a[:, :320], (4*k, 8*k), (0, 1)),
                                            np.roll(a[:, 320:], (2*k, -6*k), (0, 1))], axis=1)
                else:
                    top = np.concatenate([np.roll(a[:200, :320], (4*k, 8*k), (0, 1)),
                                          np.roll(a[:200, 320:], (2*k, -6*k), (0, 1))], axis=1)
                    bottom = np.concatenate([np.roll(a[200:, :320], (-5*k, 3*k), (0, 1)),
                                             np.roll(a[200:, 320:], (-3*k, -4*k), (0, 1))], axis=1)
                    image = np.concatenate([top, bottom], axis=0)
            elif name == 'dynamic':
                step = steps[i-1] if i else (0, 0)
                offset_x += step[0]; offset_y += step[1]
                image = np.roll(a, (offset_y, offset_x), (0, 1))
                expected.append(dict(frame=i, forward_dx_px=step[0], forward_dy_px=step[1]))
            else:
                x, y = k * dx, k * dy
                ix, iy = int(x), int(y)
                fx, fy = x - ix, y - iy
                image = ((1-fx)*(1-fy)*np.roll(a, (iy, ix), (0, 1)) +
                         fx*(1-fy)*np.roll(a, (iy, ix+1), (0, 1)) +
                         (1-fx)*fy*np.roll(a, (iy+1, ix), (0, 1)) +
                         fx*fy*np.roll(a, (iy+1, ix+1), (0, 1))).round().astype(np.uint8)
                expected.append(dict(frame=i, forward_dx_px=dx if i > 3 else 0,
                                     forward_dy_px=dy if i > 3 else 0))
            out.write(image.tobytes())
            out.write(bytes([128]) * (640 * 400 // 2))
    metadata[name] = dict(width=640, height=400, frames=frames, format='yuv420p', expected=expected)
metadata['quadrants']['quadrant_forward_dx_dy'] = [[8, 4], [-6, 2], [3, -5], [-4, -3]]
metadata['split']['half_forward_dx_dy'] = [[8, 4], [-6, 2]]
(OUT / 'fixtures.json').write_text(json.dumps(metadata, indent=2) + '\n')
