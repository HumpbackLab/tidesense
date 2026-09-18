# TideSense DATA 自动扩容

整卡镜像仍采用 BOOT / ROOTFS / DATA。`make_sdcard_image.py` 自动调用
`build.sh` 构建 A53 静态工具并打包；首次启动挂载 DATA 前离线检查并扩容至卡尾。
成功后记录卡容量，后续启动直接跳过。新镜像烧入更大的卡会自动扩容；
若克隆已扩容的系统到更大卡，已有 MBR 备份会触发保护，需要先人工检查。
ROOTFS 保持 200 MiB，本功能不合并分区。

工具仅接受当前 TideSense MBR 布局（BOOT 起点 2048、161920 扇区；
ROOTFS 起点 165888、409600 扇区；DATA 起点 575488；无第四分区）。
只修改 DATA 长度，写入前保存 `/etc/tidesense-mbr.before-grow` 并 fsync，
使用 BLKPG_RESIZE_PARTITION 通知内核，再由 e2fsck 检查、resize2fs 离线扩展 ext4，并再次检查。
SDK make_ext4fs 生成的 resize inode 曾导致在线扩容失败，因此不使用在线扩容。
布局不符、磁盘过小、已有备份但仍需要改写 MBR 时停止并报错。
若通知内核失败，保留已写入分区表，下次启动重试；不自动重启。
扩容失败不阻止系统启动，控制台会提示。不要在扩容后恢复旧分区表，
否则已扩大的文件系统会被截断。

构建：`scripts/sd-grow/build.sh`。源码固定为 e2fsprogs 1.47.2，校验 SHA256，
默认使用 SDK GCC 11，支持 CROSS_COMPILE 覆盖。首次构建需要联网下载。
GPL 许可见下载源码的 NOTICE/COPYING，镜像附带 NOTICE。

已运行设备需要安装三个构建产物至 `/sbin/` 和 `auto-grow-data` 脚本，
卸载 DATA 后执行 `/sbin/auto-grow-data`。镜像脚本负责启动钩子的安装。
