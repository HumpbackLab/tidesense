#!/usr/bin/env bash
# Build an offline-replay image; do not include the SDK's incompatible binary drivers.
set -euo pipefail
script_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
repo=$(cd "$script_dir/../../../.." && pwd)
sdk=${LUCKFOX_SDK:-/home/ncer/luckfox-vt}
work=${PROFILE_IMAGE_DIR:-$repo/install/rv1103-perf-image-20261025}
replay=${PROFILE_REPLAY_DIR:-$repo/install/rv1103-openvins-20261025}
kbuild=${PROFILE_KERNEL_BUILD:-/tmp/rv1103-profile/kernel}
cross=$sdk/tools/linux/toolchain/arm-rockchip830-linux-uclibcgnueabihf/bin/arm-rockchip830-linux-uclibcgnueabihf-
rootfs=$work/staging/rootfs
oem=$work/staging/oem
images=$work/images
[[ ! -d "$rootfs" ]] || { echo "Use a fresh PROFILE_IMAGE_DIR" >&2; exit 1; }
mkdir -p "$rootfs" "$oem" "$images" "$work/staging/userdata"
cp -a "$sdk/output/out/rootfs_uclibc_rv1106/." "$rootfs/"
cp -a "$sdk/output/out/oem/." "$oem/"
cp -a "$sdk/output/out/userdata/." "$work/staging/userdata/"

# No old .ko enters this image, including firmware copied by SDK OEM scripts.
python3 - "$rootfs" "$oem" <<'PY'
from pathlib import Path
import sys
for directory in sys.argv[1:]:
    for path in Path(directory).rglob('*.ko'):
        path.unlink()
PY
make -C "$sdk/sysdrv/source/kernel" O="$kbuild" ARCH=arm CROSS_COMPILE="$cross" \
    -j"${BUILD_JOBS:-6}" modules_prepare modules
mkdir -p "$oem/usr/ko"
python3 - "$kbuild" "$oem/usr/ko" <<'PY'
from pathlib import Path
import shutil, sys
build, target = map(Path, sys.argv[1:])
modules = [x.strip() for x in (build/'modules.order').read_text().splitlines() if x.strip()]
for name in modules:
    source = build/name
    output = target/source.name
    if output.exists():
        raise RuntimeError(f'Duplicate module name: {output}')
    shutil.copy2(source, output)
print('Copied newly built kernel modules:', len(modules))
PY
for module in "$oem"/usr/ko/*.ko; do "${cross}strip" --strip-debug "$module"; done
python3 "$script_dir/audit-module-abi.py" "$sdk/sysdrv/source/objs_kernel/kernel/module.o" \
    "$kbuild/kernel/module.o" "$oem/usr/ko" > "$work/module-abi-audit.json"

# SDK startup loads binary-only camera/media and wireless drivers. This image is
# for image+IMU replay over RNDIS; keep those services disabled in the staged copy.
for file in "$rootfs/etc/init.d/S21appinit" "$rootfs/etc/init.d/S26wifi" \
            "$oem/usr/ko/insmod_ko.sh" "$oem/usr/ko/insmod_wifi.sh"; do
    cat > "$file" <<'EOF'
#!/bin/sh
echo 'TideSense offline profiling image: automatic camera/media/Wi-Fi modules disabled'
exit 0
EOF
    chmod 755 "$file"
done
install -m755 "$replay/perf" "$rootfs/usr/bin/perf"
mkdir -p "$rootfs/usr/share/doc/perf-tip"
cp "$sdk/sysdrv/source/kernel/tools/perf/Documentation/tips.txt" "$rootfs/usr/share/doc/perf-tip/"
profile=$rootfs/root/openvins-profile-20261025
mkdir -p "$profile/bin" "$profile/share/openvins" "$profile/results"
"${cross}strip" -o "$profile/bin/openvins_mono" "$replay/build/stage/bin/openvins_replay_benchmark"
chmod 755 "$profile/bin/openvins_mono"
cp -a "$replay/build/stage/share/openvins/euroc_mav" "$profile/share/openvins/"
cp -a "$replay/dataset" "$profile/"
install -m755 "$script_dir/run-board.sh" "$profile/run-board.sh"
cat > "$rootfs/etc/tidesense-profile-image" <<'EOF'
TideSense mini RV1103 offline profiling image
Kernel: Linux 5.10.160 with PERF_EVENTS and ARM_PMU
Input: EuRoC V1_01 monocular 752x480 image+IMU replay
Camera/media and Wi-Fi auto-loading: disabled
Access: RNDIS 172.32.0.93, root/luckfox
Run: sh /root/openvins-profile-20261025/run-board.sh baseline baseline
Perf: sh /root/openvins-profile-20261025/run-board.sh perf perf
EOF
cp "$replay/kernel/boot.img" "$images/boot.img"
for name in env.img idblock.img uboot.img download.bin; do
    cp "$sdk/output/image/$name" "$images/$name"
done
cp "$sdk/.BoardConfig.mk" "$work/BoardConfig.mk"
cp "$kbuild/.config" "$work/kernel.config"

# Use dynamic e2fsprogs so fakeroot ownership is honored, with the SDK's ext4
# feature exclusions and partition sizes. Shrink the filesystems after copying.
fakeroot bash "$script_dir/mkfs-images.sh" "$work/staging" "$images"
bash "$sdk/tools/linux/Linux_Pack_Firmware/mk-update_pack.sh" -id rv1103 -i "$images"
(cd "$images" && sha256sum *.img download.bin > SHA256SUMS)
echo "Offline-replay image built: $images/update.img"
