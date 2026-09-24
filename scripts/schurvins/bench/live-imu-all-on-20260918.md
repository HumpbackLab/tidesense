# SchurVINS 实时双目＋IMU：全部优化开启实测

日期：2026-09-18。SDK 提交 `fb58dea`；SchurVINS 提交 `029d8fe`。

设备已部署程序与主机 SHA256SUMS 全部一致，六项优化本来就全部开启，本次没有关闭功能或减少特征数。
GCC 11.3.1、Cortex-A53、Release `-O3 -DNDEBUG`、主程序全静态；
`HALFSAMPLE_NEON`、`ZMSSD_NEON`、`FAST10_NEON`、`ALIGN2D_NEON`、`ALIGN2D_HESSIAN_NEON`、`WARP_AFFINE_NEON` 全为 ON，已核对实际编译参数。

输入：OV9281 双目 640×400 / 20fps、共享 FSIN，SC7U22 200Hz。
使用当前 `--vio-smoke` 占位标定，沿用用户先前说明的未安装镜头状态，不能代表有镜头、真实纹理场景的负载或定位精度。
连续三轮，每轮处理 300 对；无人工处理延迟。日志、逐帧计时和 profile 均写板上 `/tmp`，测试结束后才取回。

| 轮次 | 平均 ms | 中位数 ms | P95 ms | P99 ms | 最大 ms | 超过 50ms | 实际处理 对/秒 |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| run1 | 55.81 | 67.08 | 98.67 | 102.17 | 108.23 | 53.3% | 14.30 |
| run2 | 41.03 | 28.65 | 90.69 | 95.96 | 97.11 | 36.7% | 16.62 |
| run3 | 39.59 | 23.58 | 95.39 | 101.31 | 114.79 | 29.7% | 16.56 |

合计 900 对均 tracking=900、imu_fused=900；逐帧耗时平均 45.48ms，
按三轮处理时段合并的实际吞吐为 15.75 对/秒。耗时存在较大波动，目前不能稳定处理全部 20fps 输入。

统计口径：耗时为 `addImageBundle()` 调用的墙钟时间，包含其中的前端和 SchurVINS 惯性后端；
不包含等待相机帧、入队 IMU 等外部步骤。实际处理帧率为“处理数 ÷ 首次调用开始至末次调用结束的时间”，
包含中间等待和入队开销，但排除进程/传感器启动及退出耗时；不能用 1000/平均耗时替代实际吞吐。
P95/P99 使用线性插值分位数。去掉前 20 对后平均耗时分别为 57.38ms, 41.69ms, 40.09ms，不是单纯初始化拖慢。

| 轮次 | 采集对数 | 替换旧图像对数（含启动期） | IMU Hz | IMU 最大间隔 ms | 送入 SchurVINS 的 IMU 条数 |
| --- | ---: | ---: | ---: | ---: | ---: |
| run1 | 428 | 123 | 200.506 | 9.07402 | 4200 |
| run2 | 370 | 65 | 200.482 | 8.71692 | 3620 |
| run3 | 371 | 67 | 200.479 | 9.01936 | 3650 |

三轮软件 IMU 队列均未丢弃样本，存在主机读取时间戳抖动；不等于硬件零丢样证明。
图像处理跟不上时只保留最新图像对，惯性历史继续完整交付。
最后确认 FSIN enable=0、SPI 工作线程恢复 SCHED_OTHER/0；测试已停止。

复现单轮（在设备）：

```sh
cd /mnt/data/schurvins-gcc11-static
SCHUR_TIMING_CSV=/tmp/timing.csv SCHUR_PROFILE_CSV=/tmp/profile.csv \
  ./run.sh --vio-smoke /mnt/data/sensor_cfg.ini auto 300 > /tmp/run.log 2>&1
```

主机原始数据、构建信息、校验值、分析脚本和完整 JSON 统计：
`install/schurvins-gcc11-static/speed-all-on-20260918/`。
设备原始数据：`/tmp/schur-speed-all-on/`。
