# c906 实验与验证

从 README 整理的实验记录。日期、配置、结论及路径保留原实验语境；当前用法见本目录 README。

## README.md 实验与验证记录

### 已部署及实测

2026-10-28，在 TideSense 上连续交替加载普通固件和 TPU 测试固件 10 次，
全部成功，前后 Linux boot ID 相同。以下时间由内核测量，固件已在用户态内存中，
不包含从电脑上传、SD 读取及启动命令行进程的时间：

| 阶段 | 10 次平均 |
| --- | ---: |
| 邮箱请求到小核停驻 | 0.118 ms |
| 清空 768 KiB 保留区、复制固件及配置 | 0.786 ms |
| 新固件启动、报告就绪并通过邮箱检查 | 18.733 ms |
| 合计 | 19.640 ms，范围 19.576–19.698 ms |

实测日志保存在本机 `install/c906-reload/reload-switch.log`。
在线切换到 TPU 固件后另运行了 500 次 `y=2x+1`，2048 个 BF16 元素全部正确，
再在线切回普通固件。还验证了模块卸载后重新加载，以及拒绝未带重载钩子的
原版固件；拒绝操作后小核仍正常响应。

设备工具及两种固件位于 `/mnt/data/c906-reload/`。普通固件保留 SDK 的原有
任务和邮箱处理，TPU 及 LK 实验使用按需编译的其他固件版本。

与备份一致，核对记录在 `install/c906-reload/fip-verification.json`。
原始 FIP 同时备份于本机与设备的
`install/c906-tpu-probe-20261028/fip-before.bin` 和
`/mnt/data/c906-tpu-probe-20261028/fip-before.bin`，SHA-256：
`fb8838667ad9987a3c881ad0ace7b61a7b7e01a7662c58b87e4485db6bd9649b`。
恢复原始开机固件时，挂载 `/dev/mmcblk0p1`，将备份复制为分区根目录
`fip.bin`，同步后重启。无需替换 `boot.sd`。

## 重载实现与内存布局背景

Linux 可以将兼容的 C906 固件复制到小核保留内存并重新启动其 FreeRTOS，
不需要重启 A53，也不写 SD 卡。当前实现是小核配合的**软件重启**：旧固件
通过邮箱进入保留区外的跳板，关闭中断、清理缓存并等待；Linux 更新固件和
启动配置后放行；新固件报告邮箱就绪后才恢复通信。

- `cvirtos-reload-tpu-lk.bin/.elf`、`c906-tpu-lk`：可选 LK 原型，说明见
  [LK 实验文档](../vio-compare/probes/c906-tpu/README-LK.md)。

脚本使用独立的 `freertos/cvitek/build/c906-reload/` 任务构建目录。
FreeRTOS 的 `TIDESENSE_C906_RELOAD`、`TIDESENSE_C906_TPU_PROBE` 和
`TIDESENSE_C906_TPU_LK` 选项默认

它不是硬件复位或通用 remoteproc：当前 monitor 不支持 SDK remoteproc 使用
的启动 SMC，Linux 也不能直接写安全域入口寄存器，所以采用协作跳板方案。
任意自定义固件还需保持入口、内存布局、启动初始化和邮箱重载协议兼容。
