#!/usr/bin/env python3
"""Validate/export TideSense raw capture to EuRoC layout, SchurVINS and ROS1 bag.
Images are lossless PNG. No ROS installation required; --bag uses rosbags/numpy.
"""
import argparse
import csv
import json
import math
from pathlib import Path
import shutil
import struct
import zlib
import yaml

MAGIC = b'TIDESENSE_RAW_V1\n'
HEADER = struct.Struct('<IIIIQQQIIII')
PIXELS = 640 * 400
RECORD = HEADER.size + 2 * PIXELS


def frames(source):
    with (source / 'frames.bin').open('rb') as f:
        magic = f.read(len(MAGIC))
        compressed = magic == b'TIDESENSE_ZST_V1\n'
        if magic != MAGIC and not compressed:
            raise ValueError('Invalid archive magic')
        if compressed:
            import zstandard
            decompressor = zstandard.ZstdDecompressor()
        while True:
            if compressed:
                length = f.read(4)
                if not length:
                    return
                if len(length) != 4:
                    raise ValueError('Truncated record length')
                size = struct.unpack('<I', length)[0]
                if not 0 < size <= RECORD + 4096:
                    raise ValueError('Invalid compressed record size')
                data = f.read(size)
                if len(data) != size:
                    raise ValueError('Truncated compressed record')
                try:
                    data = decompressor.decompress(data, max_output_size=RECORD)
                except zstandard.ZstdError as exc:
                    raise ValueError(f'Frame decompression/checksum failed: {exc}') from exc
            else:
                data = f.read(RECORD)
                if not data:
                    return
            if len(data) != RECORD:
                raise ValueError('Truncated/invalid frame record; refuse export')
            h = HEADER.unpack_from(data)
            pixels = data[HEADER.size:]
            if compressed:
                import numpy as np
                residual = np.frombuffer(pixels, dtype=np.uint8).reshape(800, 640)
                pixels = np.cumsum(residual, axis=1, dtype=np.uint8).tobytes()
            yield h, pixels[:PIXELS], pixels[PIXELS:]


def imu_rows(source):
    with (source / 'imu.csv').open() as f:
        for row in csv.reader(line for line in f if not line.startswith('#')):
            if len(row) != 10:
                raise ValueError('IMU CSV requires timestamp + 9 SI values')
            t, values = int(row[0]), [float(v) for v in row[1:]]
            if t <= 0 or not all(math.isfinite(v) for v in values):
                raise ValueError('Invalid IMU timestamp/value')
            yield t, values


def validate(source, offset):
    if not (source / 'COMPLETE').is_file() or (source / 'INCOMPLETE').exists():
        raise ValueError('Dataset is incomplete; refuse to present it as a valid dataset')
    meta = yaml.safe_load((source / 'metadata.yaml').read_text())
    if meta.get('schema') != 'tidesense-dataset-v1' or not meta.get('complete'):
        raise ValueError('Invalid metadata schema/completion')
    first = last = previous = None
    count = 0
    for h, _, _ in frames(source):
        magic, version, width, height, seq, left, right, lr, rr, flags, reserved = h
        if (magic, version, width, height, flags, reserved) != (0x54444341, 1, 640, 400, 1, 0):
            raise ValueError('Invalid camera header or FSIN disabled')
        if not left or not right or abs(right-left) > 20000000 or left+offset <= 0:
            raise ValueError('Invalid camera timestamp/skew')
        if previous:
            if (seq != previous[4]+1 or not 0 < left-previous[5] <= 75000000
                    or not 0 < right-previous[6] <= 75000000
                    or (lr-previous[7]) % 2**32 != 1 or (rr-previous[8]) % 2**32 != 1):
                raise ValueError('Camera gap/nonmonotonic sequence')
        previous = h
        if first is None:
            first = left+offset
        last = left+offset
        count += 1
    imu_first = imu_last = None
    samples = 0
    for t, _ in imu_rows(source):
        if imu_last is not None and not 0 < t-imu_last <= 7500000:
            raise ValueError('IMU gap/nonmonotonic timestamp')
        if imu_first is None:
            imu_first = t
        imu_last = t
        samples += 1
    if count < 2 or samples < 2 or not imu_first < first <= last < imu_last:
        raise ValueError('Insufficient frames/IMU or IMU does not bracket image time range')
    if count != meta['pairs'] or samples != meta['imu_samples']:
        raise ValueError('Metadata counts differ from archive (truncation/corruption)')
    return dict(pairs=count, imu_samples=samples, camera_hz=(count-1)*1e9/(last-first),
                imu_hz=(samples-1)*1e9/(imu_last-imu_first), camera_offset_ns=offset)


def png(data):
    def chunk(kind, value):
        return struct.pack('>I', len(value)) + kind + value + struct.pack('>I', zlib.crc32(kind+value))
    scan = b''.join(b'\0'+data[i:i+640] for i in range(0, PIXELS, 640))
    return (b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR', struct.pack('>IIBBBBB', 640, 400, 8, 0, 0, 0, 0))
            + chunk(b'IDAT', zlib.compress(scan, 1))+chunk(b'IEND', b''))


def bag(source, destination, offset):
    import numpy as np
    from rosbags.rosbag1 import Writer
    from rosbags.typesys import Stores, get_typestore
    store = get_typestore(Stores.ROS1_NOETIC)
    types = store.types
    Time = types['builtin_interfaces/msg/Time']
    Header = types['std_msgs/msg/Header']
    Image = types['sensor_msgs/msg/Image']
    Imu = types['sensor_msgs/msg/Imu']
    Vector = types['geometry_msgs/msg/Vector3']
    Quaternion = types['geometry_msgs/msg/Quaternion']
    def header(t, frame):
        return Header(seq=0, stamp=Time(sec=t//10**9, nanosec=t % 10**9), frame_id=frame)
    with Writer(destination) as writer:
        cams = [writer.add_connection(f'/cam{i}/image_raw', Image.__msgtype__, typestore=store) for i in range(2)]
        inertial = writer.add_connection('/imu0', Imu.__msgtype__, typestore=store)
        def write_imu(t, v):
            covariance = np.zeros(9, dtype=np.float64)
            orientation_covariance = covariance.copy()
            orientation_covariance[0] = -1.0
            message = Imu(header(t, 'imu0'), Quaternion(0., 0., 0., 1.), orientation_covariance,
                          Vector(*v[:3]), covariance, Vector(*v[3:6]), covariance)
            writer.write(inertial, t, store.serialize_ros1(message, Imu.__msgtype__))
        samples = iter(imu_rows(source))
        current = next(samples, None)
        for h, left, right in frames(source):
            t = h[5]+offset
            while current and current[0] <= t:
                write_imu(*current)
                current = next(samples, None)
            for i, pixels in enumerate((left, right)):
                msg = Image(header(t, f'cam{i}'), 400, 640, 'mono8', 0, 640, np.frombuffer(pixels, dtype=np.uint8))
                writer.write(cams[i], t, store.serialize_ros1(msg, Image.__msgtype__))
        while current:
            write_imu(*current)
            current = next(samples, None)


def export(source, output, calibration_path=None, offset_override=None, write_bag=False):
    if calibration_path is not None and not calibration_path.is_file():
        raise ValueError(f'Calibration file missing: {calibration_path}')
    calibration_path = calibration_path or source / 'calibration.yaml'
    calibration = yaml.safe_load(calibration_path.read_text()) if calibration_path.exists() else None
    if calibration is not None and (not isinstance(calibration, dict) or len(calibration.get('cameras', [])) != 2):
        raise ValueError('Expected stereo SchurVINS calibration')
    offset = int(calibration.get('live_camera_time_offset_ns', 0)) if calibration else 0
    if offset_override is not None:
        offset = offset_override
    if abs(offset) > 10**9:
        raise ValueError('Camera offset must be within +/-1 second')
    report = validate(source, offset)
    if write_bag:
        import rosbags.rosbag1  # Fail before creating destination if dependency is missing.
        import numpy
    output.mkdir(parents=False, exist_ok=False)
    (output / 'INCOMPLETE').write_text('Export in progress\n')
    for cam in ('cam0', 'cam1'):
        (output / 'mav0' / cam / 'data').mkdir(parents=True)
    (output / 'mav0/imu0').mkdir()
    with (output / 'stereo.csv').open('w') as stereo, \
         (output / 'mav0/cam0/data.csv').open('w') as c0, \
         (output / 'mav0/cam1/data.csv').open('w') as c1, \
         (output / 'frames.csv').open('w') as raw:
        stereo.write('#timestamp_ns,left_image,right_image\n')
        for f in (c0, c1):
            f.write('#timestamp [ns],filename\n')
        raw.write('#export_ns,sequence,left_pts_ns,right_pts_ns,left_ref,right_ref,flags\n')
        for h, left, right in frames(source):
            t = h[5]+offset
            names = []
            for i, pixels in enumerate((left, right)):
                path = f'mav0/cam{i}/data/{t}.png'
                (output / path).write_bytes(png(pixels))
                names.append(path)
            stereo.write(f'{t},{names[0]},{names[1]}\n')
            for f in (c0, c1):
                f.write(f'{t},{t}.png\n')
            raw.write(','.join(map(str, (t, *h[4:10])))+'\n')
    with (output / 'imu.csv').open('w') as imu, (output / 'imu-raw.csv').open('w') as raw:
        imu.write('#timestamp_ns,gx,gy,gz,ax,ay,az\n')
        raw.write('#timestamp_ns,gx,gy,gz,ax,ay,az; before six-face correction\n')
        for t, values in imu_rows(source):
            imu.write(str(t)+','+','.join(format(v, '.17g') for v in values[:6])+'\n')
            raw.write(str(t)+','+','.join(format(v, '.17g') for v in values[:3]+values[6:])+'\n')
    shutil.copyfile(output / 'imu.csv', output / 'mav0/imu0/data.csv')
    if calibration:
        shutil.copyfile(calibration_path, output / 'calibration.yaml')
        calibration['live_camera_time_offset_ns'] = 0
        calibration.setdefault('imu_params', {})['delay_imu_cam'] = 0.0
        (output / 'replay.yaml').write_text(yaml.safe_dump(calibration, sort_keys=False))
    for name in ('imu-calibration.yaml', 'sensor_cfg.ini', 'metadata.yaml'):
        if (source / name).exists():
            shutil.copyfile(source / name, output / ('capture-'+name if name == 'metadata.yaml' else name))
    if write_bag:
        bag(source, output / 'dataset.bag', offset)
    report.update(schur_replay_ready=bool(calibration), image_orientation='original, not rectified or rotated',
                  stereo_timestamps='both use cam0 PTS plus offset; raw left/right PTS in frames.csv',
                  imu='SI, six-face applied as specified in imu-calibration.yaml; never apply twice',
                  bag_topics=['/cam0/image_raw', '/cam1/image_raw', '/imu0'] if write_bag else [])
    (output / 'export.json').write_text(json.dumps(report, indent=2)+'\n')
    (output / 'INCOMPLETE').rename(output / 'COMPLETE')
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source', type=Path)
    parser.add_argument('output', type=Path)
    parser.add_argument('--calibration', type=Path, help='SchurVINS YAML; default snapshot from capture')
    parser.add_argument('--time-offset-ns', type=int, help='Override cam0 shift; 0 retains raw PTS for recalibration')
    parser.add_argument('--bag', action='store_true', help='Also create ROS1 dataset.bag for OpenVINS/VINS-Fusion')
    args = parser.parse_args()
    try:
        print(json.dumps(export(args.source, args.output, args.calibration, args.time_offset_ns, args.bag), indent=2))
    except (ValueError, OSError, ImportError, KeyError, yaml.YAMLError) as exc:
        parser.exit(1, f'Export failed: {exc}\n')


if __name__ == '__main__':
    main()
