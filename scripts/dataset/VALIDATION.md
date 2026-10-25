# tide_cam 接入验证

2026-10-25：通用录制器已通过 tide_cam 在进程内采集，无相机 INI 或辅助进程。
640×400 / 640×480 的连续短段和 1280×800 的 1 秒短段均正常完成，
原图、IMU、时间戳经 ROS1 导出逐项核对一致。1280×800 持续 20 Hz 录制触发写队列上限，
明确标记 INCOMPLETE 并被导出器拒绝；不保证该尺寸长时间持续录制。
完整记录见 [公共库验证记录](../../tide_cam/VALIDATION.md#数据录制与-vio-接入2026-10-25)。

# 2026-09-24 TideSense 板端验证

设备 192.168.123.99，A53 `clk_a53=1000000000`，SD async，双 OV9281 640×400，SC7U22 IIO。
无 VINS、ROS 或 DDS。此项历史录制使用此前的固定曝光配置：相机日志实际曝光
1996 µs，模拟增益 4×，数字/ISP 增益 1×；当前相机 helper 使用自动曝光且上限 2 ms。

## 写盘瓶颈与处理

128 MiB 顺序写、包含 fsync：14.62 s，即约 9.18 MB/s。
MMC 实际时钟 23.4375 MHz，4 bit。未压缩双目需要 10.24 MB/s，顺序写未压缩原型在 322 对时队列满，
正确留下 INCOMPLETE，未把数据丢失伪装成有效录制。

最终实现使用水平差分 + zstd level 1，无损，独立帧带 checksum。

## 60 秒连续采集

录制：`/mnt/data/dataset-zstd-validation-20260924/`。

| 指标 | 实测 |
| --- | ---: |
| 双目帧对 | 1,199 |
| IMU 样本 | 12,009 |
| 图像时间范围 | 60.039509 s |
| 双目实际频率 | 19.95353 Hz |
| IMU 实际频率 | 199.53536 Hz |
| 最大相机间隔 | 50.251 ms |
| 最大 IMU 间隔 | 5.0616 ms |
| 写队列峰值 | 7 / 64 对 |
| 图像归档大小 | 275,990,520 字节（含文件头） |
| 压缩图像写入率 | 约 4.60 MB/s |
| 差分+压缩平均耗时 | 33.64 ms/对 |

相机 `pairing_discarded=0`、`replaced_pending=0`。记录区间帧序、左右参考号连续；
IMU 无 >7.5 ms 间隔。正常写入 COMPLETE。启动/收尾样本不计入已录制区间。
19.95/199.54 为当前传感器时钟的实测速率，20/200 为标称值；未重采样或伪造时间戳。

另测 SIGINT 停止：102 对、1,037 条 IMU，排空写队列后生成 COMPLETE。
该次同时有 SD 数据回传，队列峰值 40/64，说明剩余空间可吸收短暂 I/O 竞争。
结束后的未录制 IMU 样本出现一次 14.26ms 间隔，不属于已提交的数据区间。

## 格式与错误路径

- Python 8 项测试通过：无损 PNG/ROS1 bag 读回、差分+zstd逐字节还原、checksum 错误、截断、IMU缺口、INCOMPLETE、无标定导出及时间偏移覆盖校验。
- 板端 camera mock 通过：原 VIO 最新帧语义、录制 FIFO 无覆盖、FIFO 满报错、非法头、截断、时间倒退和进程回收。
- 实录完整导出 EuRoC PNG + CSV、SchurVINS replay 文件、ROS1 dataset.bag。
- 独立读取实录 ROS1 bag：左右各 1,199 张 mono8 640×400 图像，IMU 12,009 条；全部 header stamp 等于 bag 时间，时间排序正确，双目共享校准后帧时刻，IMU 包围图像区间。
- 板端 SchurVINS 原有 `--replay` 读取全部导出数据：`frames=1199 tracking=0 imu_fused=1199`。
  当前测试画面持续报告三角化点不足，因此估计器最终返回 1（未达到跟踪帧检查）；没有格式/读图/IMU 时间戳错误。
  这验证了回放输入兼容性，未验证成功初始化或轨迹精度。
- 本次未执行 OpenVINS/VINS-Fusion 算法；ROS1 bag 格式已独立读回验证，两者仍需自己的 TideSense 标定配置。

原始日志、导出与校验结果保存在 SDK 的 `install/dataset-validation-20260924/`，未加入 Git。

复测：

```sh
install/dataset-test-venv/bin/python scripts/dataset/test_export.py
cmake -S scripts/dataset -B install/tidesense-dataset-build -DDATASET_BUILD_TESTS=ON
cmake --build install/tidesense-dataset-build -j4
# 将 dataset_camera_{mock,test} 复制到板上 /tmp，然后：
/tmp/dataset_camera_test /tmp/dataset_camera_mock
```
