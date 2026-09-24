#!/usr/bin/env python3
import json, pathlib, sys
p=pathlib.Path(sys.argv[1]); data=json.loads((p/'summary.json').read_text())
assert len(data['runs'])==8
assert len(set(r['pose_sha256'] for r in data['runs']))==1
assert all(r['tracking']==501 for r in data['runs'])
assert all(r['tx']['errors']==r['tx']['queue_drops']==0 for r in data['runs'])
assert all(r['rx']['packets']==r['tx']['packets'] and r['rx']['complete']==r['tx']['messages'] for r in data['runs'])
names={'base':'不发送（同样初始化缓冲区，发送线程阻塞）','pose':'每帧位姿',
       'image2':'每帧位姿＋每 10 帧一张左图','image5':'每帧位姿＋每 4 帧一张左图'}
lines=['# TideSense 基础发送开销实测','',
'## 条件','',
'- 单 A53，800 MHz，GCC 11 静态编译，六项 NEON 优化开启。',
'- 同一个实验可执行文件，501 帧 EuRoC 双目＋IMU 输入，每种模式两轮，顺序正反各一次。',
'- 每批预读 64 对图像，再连续计算；统计不包含批次之间的 SD 读图等待。',
'- 每轮排除前 5 帧，两轮合计每种模式 992 个计时样本。',
'- 实际 UDP 网络发送至 192.168.123.2:39281，原始左图为 752×480 灰度图。',
'- 发送线程 nice +5，信号量阻塞唤醒，4 个预分配 1 MiB 缓冲区；基线保持相同线程和缓冲区初始化。',
'- 计算步骤包含 IMU 输入、VIO、位姿提取和同步入队；算法时间只包含 addImageBundle。',
'', '## 结果','',
'| 模式 | 算法均值 ms | 算法耗时增加 | 计算步骤均值 ms | 计算步骤耗时增加 | 算法 P95 / P99 ms |',
'|---|---:|---:|---:|---:|---:|']
for mode,g in data['groups'].items():
    q=g['pooled_vio']
    lines.append(f"| {names[mode]} | {g['vio_mean_ms']:.3f} | {g['vio_increase_percent']:+.2f}% | {g['step_mean_ms']:.3f} | {g['step_increase_percent']:+.2f}% | {q['p95']:.3f} / {q['p99']:.3f} |")
lines+=['','## 重复性与传输检查','', '| 运行 | 算法均值 ms | 计算步骤均值 ms | 发送图像数 | 接收完整消息数 |', '|---|---:|---:|---:|---:|']
for r in data['runs']:
    lines.append(f"| {r['name']} | {r['vio']['mean']:.3f} | {r['step']['mean']:.3f} | {int(r['tx']['images'])} | {r['rx']['complete']} |")
lines+=['','- 八次运行全部跟踪 501 帧，轨迹 CSV 的 SHA-256 完全一致：',
        '  `'+data['runs'][0]['pose_sha256']+'`。',
        '- 所有发送端错误、队列丢弃、接收端缺片均为 0；发送和接收的数据包数一致。',
        '- 每轮位姿模式 501 个数据包 / 44,088 字节；每 10 帧图像模式 15,801 个包 / 18,697,848 字节；每 4 帧图像模式 38,301 个包 / 46,129,848 字节。以上含实验分片头，不含 UDP/IP/USB 开销。',
        '', '## 结论与边界','',
        '- 基础位姿发送开销较小，原始图像发送产生可见的 CPU 竞争；最终百分比以表中两轮均值为准。',
        '- 每 10 / 4 帧发送图像，对 20 FPS 输入分别对应 2 / 5 Hz。测试为不限速分批回放，不能称为连续墙钟 2 / 5 Hz 实时测试。',
        '- 这是传输原型：没有 ROS/DDS、特征/地图点打包、压缩、可靠重传、相机实时采集和断线恢复。',
        '- 基线算法已有超过 50 ms 的帧；平均值不能用于保证每帧满足 20 FPS 时限。P95/P99 保留供判断。',
        '- 两轮可用于初步开销判断，尚不能替代长期运行和实时相机端到端延迟测试。',
        '- 初步轮询版结果不纳入最终表：其每毫秒轮询和初始化差异引入额外干扰。正式版本改用阻塞唤醒并统一初始化后，八轮轨迹完全一致。',
        '', '## 文件','',
        '- `summary.json` / `summary.txt`：汇总与逐轮指标。',
        '- `*-vio.csv`、`*-send.csv`：逐帧原始计时；`*-poses.csv`：轨迹。',
        '- `*-receiver.json`、`*-run.log`：收发统计；`*-environment.txt`：频率、温度、CPU 统计。',
        '- `artifacts.json`、`entrypoint.cpp.txt`、`sender.h.txt`：可执行文件/依赖哈希和测试源码快照。',
        '- 没有改动正式 SchurVINS 源码、正式可执行文件或 SD 卡镜像；板端本次临时可执行文件及日志已由运行脚本清理。','']
(p/'REPORT.md').write_text('\n'.join(lines))
print(p/'REPORT.md')
