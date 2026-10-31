# TideSense C906 固件在线重载

Linux 可在线更新兼容的 C906 FreeRTOS 固件，不重启 A53、不写 SD 卡。
这是旧固件配合的软件重启；小核死锁或新固件启动失败时仍需整板重启。

## 构建

先构建 TideSense SDK，提供 FreeRTOS 库及与设备匹配的内核/邮箱符号表。
在工程根目录执行：

```sh
scripts/c906/build.sh
scripts/c906/build.sh --tpu-probe
scripts/c906/build.sh --lk-probe
scripts/c906/build.sh --frontend-probe
```

后面三项按需生成实验固件。输出默认在 `install/c906-reload/`，
可用 `C906_OUT` 指定目录，包括加载器 `c906_loader.ko`、工具 `c906-reload`、
普通固件 `cvirtos-reload-base.bin/.elf` 及所选实验产物。

FreeRTOS 功能开关默认关闭，构建使用独立任务目录，不覆盖开机 FIP。
当前子库已接入钩子；旧版匹配源码可检查并应用：

```sh
git -C freertos apply --check ../scripts/c906/freertos-hooks.patch
git -C freertos apply ../scripts/c906/freertos-hooks.patch
```

## 日常更新（板端）

先停止摄像头、推理及其他邮箱使用者，确保没有 C906/TPU 任务执行。
将工具及兼容固件上传到 `/mnt/data/c906-reload/` 后执行：

```sh
cd /mnt/data/c906-reload
[ -e /dev/c906-loader ] || /sbin/insmod ./c906_loader.ko
./c906-reload status
./c906-reload load ./cvirtos-reload-base.bin
./c906-reload status
```

`echo=0 reload=1` 表示响应且支持重载；成功时 `result=0 phase=3`。
加载器只锁定自身操作，不阻止其他驱动使用邮箱。整板重启后仍加载 BOOT 的 FIP。

## 首次启用与恢复

原 SDK 固件不支持重载。首次需将兼容普通固件写入 FIP 并重启一次；
以本板原 FIP 为基础，仅替换 `BLCP_2ND`：

```sh
python3 fsbl/plat/cv181x/fiptool.py genfip \
  --OLD_FIP install/c906-tpu-probe-20261028/fip-before.bin \
  --BLCP_2ND install/c906-reload/cvirtos-reload-base.bin \
  install/c906-reload/fip-reload-base.bin
```

输入必须是带 `TS_C906_RELOAD_V1` 的平坦 `.bin`，不是 ELF/FIP；
仅适用当前非 fast-image 布局，地址 `0x8ff40000`、保留区 768 KiB。
任意自定义固件还需保持启动和邮箱协议兼容，签名只用于兼容性检查。
失败后不要强制卸载加载器，其内存可能仍被小核使用；需整板重启恢复。
恢复原开机固件时，将原 FIP 备份复制到 SD 第一分区根目录的 `fip.bin`，
同步后重启，无需替换 `boot.sd`。

[LK 原型](../vio-compare/probes/c906-tpu/README-LK.md) ·
[完整前端](../vio-compare/openvins/README-frontend.md) ·
[重载实测与备份校验](VALIDATION.md)
