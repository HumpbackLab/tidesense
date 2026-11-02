# SG2002 A53 临时调频实验

仅用于 TideSense 的临时性能实验，不随 `build_all` 构建或装入 SD 镜像。
电脑端命令从工程根目录执行。

## 已知边界

2026-11-02，本板从 1.10 GHz 切换到 1.15 GHz 后立即失联，尚未开始该档位的负载测试。
SSH、ping 均不可用，需要断电重上电；没有取得失联时的内核日志，不能区分
CPU 超频不稳定与 PLL 在线切换问题。1.20 GHz 未测试。
因此模块现限制为 **1000–1100 MHz**，每档 25 MHz。1.10 GHz 离线回放通过，
但后续实时相机测试在启动阶段以 139 退出，未完成；不能将此档位视为稳定配置。
设备继续使用原来的 1 GHz。

模块不修改电压、DDR、外设时钟或启动文件。仅接受 MPLL 为 CPU 父时钟且两者
均为 1 GHz 的初始状态，加载时也必须保持 1 GHz。测试通过 Linux CCF 调整 MPLL。
退出时恢复原频率和原 PLL CSR，包括驱动改写的 ICTRL 位。

模块每秒检查温度，在达到 70°C、温度读取失败或本档持续超时时尝试恢复。
`timeout_s` 默认 180 秒，允许 10–300 秒；每次成功设置非基线频率重新计时。
**这些保护依赖内核继续执行，无法从 CPU 卡死中自行恢复。**
硬件失联需断电重上电；由于没有修改启动配置，重新启动仍为原来的 1 GHz。

## 构建与短时使用

需先构建与设备匹配的 TideSense ARM64 内核，生成 `Module.symvers`：

```sh
bash scripts/perf/clock-trial/build.sh
scp install/tidesense-clock-trial/tide_clock_trial.ko root@192.168.123.99:/mnt/data/
```

以下在设备上以 root 运行，先确保没有录制等任务占用设备：

```sh
busybox insmod /mnt/data/tide_clock_trial.ko timeout_s=30
echo 1050 > /sys/module/tide_clock_trial/parameters/mhz
cat /sys/kernel/debug/clk/clk_a53/clk_rate
cat /sys/class/thermal/thermal_zone0/temp

# 测完主动恢复并卸载；正常卸载也会恢复原频率及 PLL CSR。
echo 1000 > /sys/module/tide_clock_trial/parameters/mhz
busybox rmmod tide_clock_trial
cat /sys/kernel/debug/clk/clk_a53/clk_rate
```

不要设置开机自动加载或用循环写参数延长超时。实验结果见
[调频验证记录](../VALIDATION.md#2026-11-02临时超频实验)。
