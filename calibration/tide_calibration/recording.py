"""Read and validate TideSense recordings and serialize one raw-time ROS1 bag."""
import csv
import math
import struct

import yaml

MAGIC = b'TIDESENSE_RAW_V1\n'
HEADER = struct.Struct('<IIIIQQQIIII')
RESOLUTIONS = {(640, 400), (640, 480), (1280, 800)}
MAX_RECORD = HEADER.size + 2 * 1280 * 800


def image_bytes(header):
    if (header[0], header[1], header[9], header[10]) != (0x54444341, 1, 1, 0):
        raise ValueError('Invalid camera header or FSIN disabled')
    if (header[2], header[3]) not in RESOLUTIONS:
        raise ValueError('Unsupported capture resolution')
    return header[2] * header[3]


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
                if not 0 < size <= MAX_RECORD + MAX_RECORD // 128 + 4096:
                    raise ValueError('Invalid compressed record size')
                data = f.read(size)
                if len(data) != size:
                    raise ValueError('Truncated compressed record')
                try:
                    content_size = zstandard.frame_content_size(data)
                    if content_size not in (zstandard.CONTENTSIZE_UNKNOWN, zstandard.CONTENTSIZE_ERROR) and content_size > MAX_RECORD:
                        raise ValueError('Oversized decompressed frame')
                    data = decompressor.decompress(data, max_output_size=MAX_RECORD)
                except zstandard.ZstdError as exc:
                    raise ValueError(f'Frame decompression/checksum failed: {exc}') from exc
            else:
                header_data = f.read(HEADER.size)
                if not header_data:
                    return
                if len(header_data) != HEADER.size:
                    raise ValueError('Truncated frame header')
                count = image_bytes(HEADER.unpack(header_data))
                data = header_data + f.read(2 * count)
            if len(data) < HEADER.size:
                raise ValueError('Truncated/invalid frame record; refuse export')
            h = HEADER.unpack_from(data)
            count = image_bytes(h)
            if len(data) != HEADER.size + 2 * count:
                raise ValueError('Truncated/invalid frame record; refuse export')
            pixels = data[HEADER.size:]
            if compressed:
                import numpy as np
                residual = np.frombuffer(pixels, dtype=np.uint8).reshape(2 * h[3], h[2])
                pixels = np.cumsum(residual, axis=1, dtype=np.uint8).tobytes()
            yield h, pixels[:count], pixels[count:]


def imu_rows(source):
    with (source / 'imu.csv').open() as f:
        for row in csv.reader(line for line in f if not line.startswith('#')):
            if len(row) not in (7, 10):
                raise ValueError('IMU CSV requires timestamp + 6 SI values (or legacy 9-value rows)')
            t, values = int(row[0]), [float(v) for v in row[1:]]
            if t <= 0 or not all(math.isfinite(v) for v in values):
                raise ValueError('Invalid IMU timestamp/value')
            # Legacy archives carry the original accelerometer in the last three columns.
            yield t, values if len(values)==6 else values[:3]+values[6:]


def validate(source):
    if not (source / 'COMPLETE').is_file() or (source / 'INCOMPLETE').exists():
        raise ValueError('Dataset is incomplete; refuse to present it as a valid dataset')
    meta = yaml.safe_load((source / 'metadata.yaml').read_text())
    if not isinstance(meta, dict) or meta.get('schema') not in ('tidesense-dataset-v1', 'tidesense-dataset-v2') or not meta.get('complete'):
        raise ValueError('Invalid metadata schema/completion')
    first = last = previous = None
    count = 0
    resolution = None
    for h, _, _ in frames(source):
        magic, version, width, height, seq, left, right, lr, rr, flags, reserved = h
        image_bytes(h)
        if resolution is None:
            resolution = [width, height]
        if resolution != [width, height]:
            raise ValueError('Resolution changed within recording')
        if not left or not right or abs(right-left) > 20000000 or left <= 0:
            raise ValueError('Invalid camera timestamp/skew')
        if previous:
            if (seq != previous[4]+1 or not 0 < left-previous[5] <= 75000000
                    or not 0 < right-previous[6] <= 75000000
                    or (lr-previous[7]) % 2**32 != 1 or (rr-previous[8]) % 2**32 != 1):
                raise ValueError('Camera gap/nonmonotonic sequence')
        previous = h
        if first is None:
            first = left
        last = left
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
    if 'width' in meta and [meta['width'], meta.get('height')] != resolution:
        raise ValueError('Metadata dimensions differ from archive')
    return dict(pairs=count, imu_samples=samples, resolution=resolution, camera_hz=(count-1)*1e9/(last-first),
                imu_hz=(samples-1)*1e9/(imu_last-imu_first), camera_offset_ns=0)



def write_bag(source, destination):
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
            t = h[5]
            while current and current[0] <= t:
                write_imu(*current)
                current = next(samples, None)
            for i, pixels in enumerate((left, right)):
                msg = Image(header(t, f'cam{i}'), h[3], h[2], 'mono8', 0, h[2], np.frombuffer(pixels, dtype=np.uint8))
                writer.write(cams[i], t, store.serialize_ros1(msg, Image.__msgtype__))
        while current:
            write_imu(*current)
            current = next(samples, None)
