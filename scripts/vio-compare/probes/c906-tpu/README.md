# C906 直接访问 SG2002 TPU 实验

固件通过 MMIO 提交 TDMA/TIU 描述符并轮询完成。Linux 仅准备共享 ION
缓冲区、描述符和时钟，不调用 TPU 提交接口。该实验验证访问权限、命令格式、
缓存交接和提交开销，还没有实现 LK。

后续 LK 原型、精度/耗时及 A53 并行验证见 [README-LK.md](README-LK.md)。

从 SDK 根目录构建：

```sh
scripts/c906/build.sh --tpu-probe
```

输出在 `install/c906-reload/`。将 `cvirtos-reload-tpu.bin`、
`c906_tpu_bridge.ko` 和 `c906-tpu-host` 上传设备的 `/mnt/data/c906-reload/`。
小核在线重载方法与首次启动准备见 [C906 重载说明](../../../c906/README.md)。
设备上停止摄像头、推理和其他邮箱使用者后执行：

```sh
cd /mnt/data/c906-reload
./c906-reload load ./cvirtos-reload-tpu.bin
/sbin/rmmod cv181x_tpu
/sbin/insmod ./c906_tpu_bridge.ko
./c906-tpu-host read
./c906-tpu-host copy 100 5
./c906-tpu-host affine 100 5
```

`affine` 在 2048 个 BF16 元素上计算 `y=2x+1`，每次都与 A53 参考结果比较。
2026-10-28 在线重载后的 5 组、每组 100 次运行全部正确。C906 内一次提交及等待
平均 6.46 µs；100 次的完整 A53 往返平均 859 µs，包括共享缓冲区缓存维护，
不能将这两个数字视作 LK 的预计耗时。

结束后恢复 Linux TPU 驱动及普通小核固件：

```sh
/sbin/rmmod c906_tpu_bridge
/sbin/insmod /mnt/system/ko/cv181x_tpu.ko
./c906-reload load ./cvirtos-reload-base.bin
```

TPU 在实验期间归 C906 独占；桥接模块拒绝在 Linux TPU 驱动绑定时加载。
固件中的 TPU 中断输出关闭，不给 Linux 发送完成中断。
