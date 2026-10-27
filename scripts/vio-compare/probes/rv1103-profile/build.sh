#!/usr/bin/env bash
# Isolated Cortex-A7 FP64 replay, RV1103 perf kernel, and static perf recorder.
set -euo pipefail
script_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
repo=$(cd "$script_dir/../../../.." && pwd)
work=${PROFILE_BUILD_DIR:-$repo/install/rv1103-openvins-20261025}
sdk=${LUCKFOX_SDK:-/home/ncer/luckfox-vt}
reference=${OPENVINS_REFERENCE:-/home/ncer/fpga_openvins}
kernel=$sdk/sysdrv/source/kernel
cross=$sdk/tools/linux/toolchain/arm-rockchip830-linux-uclibcgnueabihf/bin/arm-rockchip830-linux-uclibcgnueabihf-
jobs=${BUILD_JOBS:-4}
mkdir -p "$work/src/benchmarks" "$work/src/third_party" "$work/kernel-build" "$work/perf-build" "$work/kernel"
if [[ ! -f "$work/src/third_party/open_vins/ov_core/src/utils/replay_profile.h" ]]; then
    cp -a "$reference/benchmarks/openvins_replay" "$reference/benchmarks/replay_common" "$work/src/benchmarks/"
    cp -a "$reference/third_party/open_vins" "$work/src/third_party/"
    python3 "$script_dir/instrument.py" "$work/src"
fi
BUILD_DIR="$work/build" BUILD_JOBS="$jobs" bash "$work/src/benchmarks/openvins_replay/build.sh" a7 -DOPENVINS_FP32_BACKEND=OFF

if [[ ! -f "$work/kernel-build/.config" ]]; then
    cp "$sdk/sysdrv/source/objs_kernel/.config" "$work/kernel-build/.config"
fi
while IFS='=' read -r key value; do
    [[ "$value" == y ]] && "$kernel/scripts/config" --file "$work/kernel-build/.config" -e "${key#CONFIG_}"
done < "$script_dir/perf.config"
make -C "$kernel" O="$work/kernel-build" ARCH=arm CROSS_COMPILE="$cross" olddefconfig
make -C "$kernel" O="$work/kernel-build" ARCH=arm CROSS_COMPILE="$cross" -j"$jobs" zImage rv1103g-luckfox-pico.dtb
# Package directly: SDK .img target tries to rewrite logos in the source tree.
cp "$work/kernel-build/arch/arm/boot/zImage" "$work/kernel/kernel"
cp "$work/kernel-build/arch/arm/boot/dts/rv1103g-luckfox-pico.dtb" "$work/kernel/fdt"
cp "$sdk/sysdrv/source/objs_kernel/resource.img" "$work/kernel/resource"
sed 's/arch = ""/arch = "arm"/g;s/compression = ""/compression = "none"/' "$kernel/boot.its" > "$work/kernel/boot.its"
(cd "$work/kernel" && mkimage -E -p 0x800 -f boot.its boot.img)
cp "$work/kernel-build/.config" "$work/kernel/config"

perf_args=(O="$work/perf-build/" ARCH=arm CROSS_COMPILE=arm-linux-gnueabihf- LDFLAGS=-static
    NO_LIBELF=1 NO_LIBPERL=1 NO_LIBPYTHON=1 NO_SLANG=1 NO_GTK2=1 NO_LIBUNWIND=1
    NO_LIBDW_DWARF_UNWIND=1 NO_DWARF=1 NO_LIBBPF=1 NO_LIBAUDIT=1 NO_LIBNUMA=1
    NO_LIBCRYPTO=1 NO_LIBCAP=1 NO_LIBBABELTRACE=1 NO_LIBZSTD=1 NO_LIBDEBUGINFOD=1
    NO_AUXTRACE=1 NO_JVMTI=1 NO_SDT=1 NO_ZLIB=1 NO_LZMA=1 WERROR=0)
make -C "$kernel/tools/perf" -f Makefile.perf "${perf_args[@]}" fixdep
# Recording PMU events needs neither tracepoint plugins nor a dynamic symbol list.
# Do not link shared plugins with the static executable's LDFLAGS.
printf '{\n};\n' > "$work/perf-build/libtraceevent-dynamic-list"
make -C "$kernel/tools/perf" -f Makefile.perf FIXDEP=1 "${perf_args[@]}" -j"$jobs" \
    -o libtraceevent_plugins -o "$work/perf-build/libtraceevent-dynamic-list" all
arm-linux-gnueabihf-strip -o "$work/perf" "$work/perf-build/perf"
python3 "$script_dir/audit-module-abi.py" \
    "$sdk/sysdrv/source/objs_kernel/kernel/module.o" "$work/kernel-build/kernel/module.o" \
    "$sdk/output/out/oem/usr/ko" > "$work/kernel/module-abi-audit.json" || {
    echo "Kernel and existing OEM drivers have incompatible module layouts; rebuild and validate all drivers before deployment." >&2
    exit 1
}
echo "Replay: $work/build/stage/bin/openvins_replay_benchmark"
echo "Kernel: $work/kernel/boot.img"
echo "Recorder: $work/perf (symbol decoding uses host perf)"
