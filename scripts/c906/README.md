# TideSense C906 固件在线重载

Linux 可以将兼容的 C906 固件复制到小核保留内存并重新启动其 FreeRTOS，
不需要重启 A53，也不写 SD 卡。当前实现是小核配合的**软件重启**：旧固件
通过邮箱进入保留区外的跳板，关闭中断、清理缓存并等待；Linux 更新固件和
启动配置后放行；新固件报告邮箱就绪后才恢复通信。

## 已部署及实测

2026-10-28，在 TideSense 上连续交替加载兼容的小核固件 10 次，
全部成功，前后 Linux boot ID 相同。以下时间由内核测量，固件已在用户态内存中，
不包含从电脑上传、SD 读取及启动命令行进程的时间：

| 阶段 | 10 次平均 |
| --- | ---: |
| 邮箱请求到小核停驻 | 0.118 ms |
| 清空 768 KiB 保留区、复制固件及配置 | 0.786 ms |
| 新固件启动、报告就绪并通过邮箱检查 | 18.733 ms |
| 合计 | 19.640 ms，范围 19.576–19.698 ms |

实测日志保存在本机 `install/c906-reload/reload-switch.log`。
还验证了模块卸载后重新加载，以及拒绝未带重载钩子的
原版固件；拒绝操作后小核仍正常响应。

设备工具及两种固件位于 `/mnt/data/c906-reload/`。普通固件保留 SDK 的原有
任务和邮箱处理。

## 日常更新命令

先停止摄像头、推理程序及其他邮箱使用者，确保没有 C906 或 TPU 任务在执行。
加载器仅锁定自己的操作，无法阻止其他驱动并发使用 RTOS 邮箱。

```sh
cd /mnt/data/c906-reload
[ -e /dev/c906-loader ] || /sbin/insmod ./c906_loader.ko
./c906-reload status
./c906-reload load ./cvirtos-reload-base.bin
./c906-reload status
```

`status` 中 `echo=0 reload=1` 表示小核响应且支持重载；重载成功时 `result=0`，
`phase=3` 表示新固件已就绪。同一接口可以加载重新编译上传的其他兼容 `.bin`。

更新只影响这次运行，重启整板后仍使用 BOOT 分区 `fip.bin` 内的小核固件。
要将某个版本设为开机默认值，需要另行生成并更新 FIP。

## 编译

先正常构建 TideSense SDK，提供 FreeRTOS 依赖库、内存布局和与设备匹配的
Linux 内核及 `rtos_cmdqu` 符号表。然后在 SDK 根目录执行：

```sh
scripts/c906/build.sh
```

默认输出到 `install/c906-reload/`，可用 `C906_OUT` 指定目录：

- `c906_loader.ko`：Linux 加载器，提供 `/dev/c906-loader`。
- `c906-reload`：静态链接的 A53 命令行工具。
- `cvirtos-reload-base.bin/.elf`：普通固件及调试符号。

脚本使用独立的 `freertos/cvitek/build/c906-reload/` 任务构建目录。
FreeRTOS 的 `TIDESENSE_C906_RELOAD` 选项默认
为 OFF，不会要求其他 SDK 固件启用此功能。启用重载时，启动栈从 128 KiB
调为 120 KiB，为钩子留出空间；任务堆保持 512 KiB。

当前 FreeRTOS 仓库已经接入钩子。由于 SDK 外层不跟踪该目录，另保存
`freertos-hooks.patch`；在没有这些改动的匹配版本中可先检查并应用：

```sh
git -C freertos apply --check ../scripts/c906/freertos-hooks.patch
git -C freertos apply ../scripts/c906/freertos-hooks.patch
```

## 首次启动准备与恢复

旧固件也必须带重载钩子，原始 SDK 固件无法直接使用此接口。首次部署需要
把兼容普通固件打进 FIP，并重启一次整板。此次测试已经完成该准备；后续更新
兼容固件走在线命令即可。生成 FIP 必须以本板原有 FIP 为基础，仅替换
`BLCP_2ND`，不要换用其他板的启动组件：

```sh
python3 fsbl/plat/cv181x/fiptool.py genfip \
  --OLD_FIP install/c906-tpu-probe-20261028/fip-before.bin \
  --BLCP_2ND install/c906-reload/cvirtos-reload-base.bin \
  install/c906-reload/fip-reload-base.bin
```

本板 BOOT 分区现已保留兼容的普通固件 FIP；BL2、monitor、U-Boot 的镜像内容
与备份一致，核对记录在 `install/c906-reload/fip-verification.json`。
原始 FIP 同时备份于本机与设备的
`install/c906-tpu-probe-20261028/fip-before.bin` 和
`/mnt/data/c906-tpu-probe-20261028/fip-before.bin`，SHA-256：
`fb8838667ad9987a3c881ad0ace7b61a7b7e01a7662c58b87e4485db6bd9649b`。
恢复原始开机固件时，挂载 `/dev/mmcblk0p1`，将备份复制为分区根目录
`fip.bin`，同步后重启。无需替换 `boot.sd`。

## 限制

仅适用于本板当前非 fast-image 的 SDK FreeRTOS，链接地址 `0x8ff40000`，
保留区为 768 KiB。输入必须是带 `TS_C906_RELOAD_V1` 签名的平坦二进制，
不是 ELF 或 FIP。签名用于兼容性检查，不是固件认证或完整性证明。

它不是硬件复位或通用 remoteproc：当前 monitor 不支持 SDK remoteproc 使用
的启动 SMC，Linux 也不能直接写安全域入口寄存器，所以采用协作跳板方案。
任意自定义固件还需保持入口、内存布局、启动初始化和邮箱重载协议兼容。

如果小核已死锁、关中断不再处理邮箱，或者新固件启动失败，该方式不能自行
救回，需要重启整板。失败后加载器故意保留模块引用及跳板内存，避免释放
小核可能仍在执行的代码页；不要强制卸载模块。
