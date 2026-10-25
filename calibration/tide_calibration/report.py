"""Summarize physical residuals, geometry and acquisition timing."""
import math
import re

import yaml

from .kalibr import LENSES


def residuals(path):
    """Keep physical residual statistics separate from Kalibr's normalized ones."""
    result = {}
    number = r'([-+\d.eE]+)'
    pattern = (r'^(Reprojection error \(cam[01]\)|Gyroscope error \(imu0\)|'
               r'Accelerometer error \(imu0\))\s+\[([^\]]+)\]:\s*mean\s+' + number
               + r',\s*median\s+' + number + r',\s*std:\s*' + number)
    for name, unit, mean, median, std in re.findall(pattern, path.read_text(), re.MULTILINE):
        values = [float(mean), float(median), float(std)]
        if not all(math.isfinite(v) and v >= 0 for v in values):
            raise ValueError('Invalid physical residual statistics in Kalibr results')
        result[name] = dict(unit=unit, mean=values[0], median=values[1], std=values[2])
    return result


def summarize(output, lens, capture, config, shifts):
    chain = yaml.safe_load((output / 'dataset-camchain-imucam.yaml').read_text())
    distortion = 'radtan' if lens == 'wide' else 'equidistant'
    for name in ('cam0', 'cam1'):
        cam = chain[name]
        if (cam['camera_model'] != 'pinhole' or cam['distortion_model'] != distortion
                or cam['resolution'] != capture['resolution']):
            raise ValueError('Kalibr output does not match the selected lens/capture resolution')
    baseline = math.sqrt(sum(row[3] ** 2 for row in chain['cam1']['T_cn_cnm1'][:3]))
    kalibr_model = LENSES[lens]
    summary = dict(lens=lens, kalibr_model=kalibr_model, camera_model='pinhole', distortion_model=distortion,
                   imu_model='scale-misalignment', capture=capture, baseline_m=baseline,
                   camera_imu_shifts_s=shifts,
                   applied_camera_offset_ns=config['live_camera_time_offset_ns'],
                   residuals=residuals(output / 'dataset-results-imucam.txt'),
                   quality='requires_review')
    rows = '\n'.join(f'| {name} | {v["mean"]:.6g} | {v["median"]:.6g} | {v["std"]:.6g} | {v["unit"]} |'
                     for name, v in summary['residuals'].items())
    (output / 'report.md').write_text(
        '# TideSense 标定结果\n\n'
        '求解和配置导出已完成，标定质量待复核。请检查角点覆盖、残差分布及独立 VIO 运行结果。\n\n'
        f'- 镜头：{lens} / {kalibr_model}\n'
        '- IMU：scale-misalignment，原始 SI 输入，陀螺加速度敏感项固定为零\n'
        f'- 图像：{capture["resolution"][0]}×{capture["resolution"][1]}，{capture["pairs"]} 对\n'
        f'- 实际采样率：相机 {capture["camera_hz"]:.4f} Hz，IMU {capture["imu_hz"]:.4f} Hz\n'
        f'- 实测基线：{baseline * 1000:.4f} mm（设计值 50 mm 仅供核对）\n'
        f'- 相机到 IMU 时间偏移：左 {shifts[0] * 1000:.6f} ms，右 {shifts[1] * 1000:.6f} ms\n'
        '- 运行时仅应用左相机时间偏移一次；t_imu = t_cam + shift\n\n'
        '| 物理残差 | 均值 | 中位数 | 标准差 | 单位 |\n| --- | --- | --- | --- | --- |\n'
        + (rows or '| 未解析到统计项，请查看原始报告 | — | — | — | — |') + '\n\n'
        '[双目报告](dataset-report-cam.pdf) · '
        '[相机–IMU 报告](dataset-report-imucam.pdf) · '
        '[原始残差摘要](dataset-results-imucam.txt) · '
        '[SchurVINS 配置](schurvins.yaml)\n')
    return summary
