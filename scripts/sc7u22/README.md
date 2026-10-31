# TideSense SC7U22 上板测试

这是独立的 Linux 用户态 SPI 测试程序，参考本机
`/home/ncer/PX4-Autopilot/src/drivers/imu/silan/sc7u22` 的寄存器、初始化和 FIFO 流程。
它通过通用内核 spidev 接口访问 IMU，不是 IIO 内核传感器驱动。

## 构建、运行

```sh
make -C scripts/sc7u22
```

板级配置已补充 SPI2 下的 `silan,sc7u22` 设备节点，并启用 `CONFIG_SPI_SPIDEV=y`。
旧内核需要用 `build_kernel` 重建并更新启动分区的 `boot.sd`，才能使用此程序。
SPI2 的物理地址是 `0x041a0000`，当前内核将其枚举为 **spi0**，因此设备名为
`/dev/spidev0.0`。`run.sh` 会按物理控制器和设备型号寻找节点，用 `driver_override`
绑定 spidev，不需要向通用驱动添加虚假的兼容设备 ID。

程序、脚本及许可文件已放在板上的 `/mnt/data/sc7u22-test/`：

```sh
cd /mnt/data/sc7u22-test
./run.sh 10 1000000  # 10 秒、1 MHz（默认）
# 已绑定 spidev 时，也可直接调用：
./sc7u22_test /dev/spidev0.0 10 1000000
```

请保持板子静止，关闭其他使用同一 IMU 的程序。测试会复位并重新配置 IMU，完成或
收到 SIGINT/SIGTERM 后关闭 FIFO 和传感器电源使能。绑定仅在当前启动期间有效。
测试结束可用 `echo spi0.0 > /sys/bus/spi/drivers/spidev/unbind` 解除绑定。

## 检查内容与限制

- SPI mode 3、8 bit、MSB first；寄存器地址 bit7 表示读取。
- `WHO_AM_I(0x01)=0x6a`，初始化配置逐项回读。
- 加速度 ±16 g、角速度 ±2000 °/s、标称 ODR 1600 Hz。
- FIFO 中大端六轴数据、温度、数据更新、溢出、饱和和全零加速度帧。
- 输出各轴均值、标准差、原始范围；加速度单位 g，陀螺仪单位 °/s。
- 静止加速度模长要求 0.5–1.5 g，温度要求 −40–105°C，六轴均须有更新；任一
  SPI/回读错误、溢出、饱和、全零加速度或采样不足时退出码非零。

这个测试验证基本通信和静态数据合理性，不验证标定精度、动态旋转响应、芯片硬件
自检、INT 引脚或长期稳定性。每次 FIFO burst 后按参考驱动切换 bypass/stream，
会丢弃部分采样；`captured_rate` 是软件取得的样本率，不能当成实际传感器 ODR。
输出轴为芯片坐标，未应用板级安装旋转。

实验记录与复测细节见 [VALIDATION.md](VALIDATION.md)。
