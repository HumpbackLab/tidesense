# A53/C906 通信与标量 LK 探针

早期独立实验，用于测邮箱往返及 C906 标量 LK；不接入 OpenVINS 默认前端。
当前 C906＋TPU 实现见 [完整前端](../openvins/README-frontend.md)。

## 邮箱探针

从工程根目录，用与设备匹配的内核构建：

```sh
make -C linux_5.10 \
  O="$PWD/linux_5.10/build/sg2002_tidesense_arm64_sd" \
  ARCH=arm64 \
  CROSS_COMPILE="$PWD/host-tools/gcc/arm-gnu-toolchain-11.3.rel1-x86_64-aarch64-none-linux-gnu/bin/aarch64-none-linux-gnu-" \
  M="$PWD/scripts/vio-compare/c906-probe" \
  KBUILD_EXTRA_SYMBOLS="$PWD/osdrv/interdrv/rtos_cmdqu/Module.symvers" modules
```

复制 `cmdqu_probe.ko` 到板端，加载后读取日志，再卸载：

```sh
/sbin/insmod ./cmdqu_probe.ko
dmesg | grep cmdqu_probe
/sbin/rmmod cmdqu_probe
```

该探针调用导出的内核 `rtos_cmdqu_send_wait()` 和普通固件的 echo 命令。
仅测 8 字节邮箱，不代表图像传输成本；标量 LK 使用 `lk_kernel.c`、
`lk_cmd_module.ko` 和实验固件，完整构建语境与测量见 [VALIDATION.md](VALIDATION.md)。
