#!/usr/bin/env bash
set -euo pipefail
repo=$(cd "$(dirname "$0")/../.." && pwd)
out=${C906_OUT:-"$repo/install/c906-reload"}
probe=OFF
lk=OFF
front=OFF
batch=${C906_LK_BATCH:-16}
if [[ ${1:-} == --tpu-probe && $# == 1 ]]; then
    probe=ON
elif [[ ${1:-} == --lk-probe && $# == 1 ]]; then
    probe=ON
    lk=ON
elif [[ ${1:-} == --frontend-probe && $# == 1 ]]; then
    probe=ON
    lk=ON
    front=ON
elif [[ $# != 0 ]]; then
    echo "usage: $0 [--tpu-probe|--lk-probe|--frontend-probe]" >&2
    exit 2
fi
lk_roi_cache=${C906_LK_ROI_CACHE:-$front}
rtos="$repo/freertos/cvitek"
riscv="$repo/host-tools/gcc/riscv64-elf-x86_64/bin/riscv64-unknown-elf-"
arm="$repo/host-tools/gcc/arm-gnu-toolchain-11.3.rel1-x86_64-aarch64-none-linux-gnu/bin/aarch64-none-linux-gnu-"
kernel="$repo/linux_5.10/build/sg2002_tidesense_arm64_sd"
[[ -f "$rtos/build/kernel/CMakeCache.txt" && -f "$kernel/Module.symvers" ]] || {
    echo 'First build the TideSense SDK (FreeRTOS dependencies and Linux modules).' >&2
    exit 1
}
mkdir -p "$out"
export PATH="$(dirname "$riscv"):$PATH"
export PROJECT_FULLNAME=sg2002_tidesense_arm64_sd DDR_64MB_SIZE=n

"${riscv}gcc" -march=rv64imafdc -mabi=lp64d -c "$repo/scripts/c906/trampoline.S" -o "$out/trampoline.o"
"${riscv}objcopy" -O binary -j .text "$out/trampoline.o" "$out/trampoline.bin"
python3 - "$out/trampoline.bin" "$repo/scripts/c906/trampoline.inc" <<'PY'
import pathlib, sys
code = pathlib.Path(sys.argv[1]).read_bytes()
assert len(code) < 512
lines = ['/* Generated from trampoline.S by build.sh. */',
         'static const unsigned char c906_reload_trampoline_code[] = {']
for i in range(0, len(code), 16):
    lines.append('    ' + ', '.join(f'0x{x:02x}' for x in code[i:i+16]) + ',')
lines.append('};\n')
target = pathlib.Path(sys.argv[2])
text = '\n'.join(lines)
if not target.exists() or target.read_text() != text:
    target.write_text(text)
PY
cmake --build "$rtos/build/common" --target install
cmake --build "$rtos/build/kernel" --target install
cmake -G Ninja -S "$rtos/task" -B "$rtos/build/c906-reload" \
    -DCMAKE_TOOLCHAIN_FILE="$rtos/scripts/toolchain-riscv64-elf.cmake" \
    -DTOP_DIR="$rtos" -DBUILD_ENV_PATH="$repo/build" -DCHIP=cv181x \
    -DRUN_ARCH=riscv64 -DRUN_TYPE=CVIRTOS -DBOARD_FPGA=n \
    -DTIDESENSE_C906_RELOAD=ON -DTIDESENSE_C906_TPU_PROBE="$probe" \
    -DTIDESENSE_C906_FRONTEND="$front" -DTIDESENSE_C906_TPU_LK="$lk" -DTIDESENSE_C906_LK_BATCH="$batch" \
    -DTIDESENSE_C906_LK_DUAL_BATCH="${C906_LK_DUAL_BATCH:-OFF}" \
    -DTIDESENSE_C906_LK_ROI_CACHE="$lk_roi_cache" \
    -DTIDESENSE_C906_LK_PROFILE_OCCUPANCY="${C906_LK_PROFILE_OCCUPANCY:-OFF}" \
    -DTIDESENSE_C906_LK_FAST_ROI="${C906_LK_FAST_ROI:-$front}" \
    -DTIDESENSE_C906_FRONT_FASTMEM="${C906_FRONT_FASTMEM:-$front}" \
    -DTIDESENSE_C906_FRONT_LATEST_INTRINSICS="${C906_FRONT_LATEST_INTRINSICS:-$front}" \
    -DTIDESENSE_C906_FRONT_PROFILE="${C906_FRONT_PROFILE:-OFF}" \
    -DTIDESENSE_C906_FRONT_IVE_HIST="${C906_FRONT_IVE_HIST:-OFF}" \
    -DTIDESENSE_C906_FRONT_IVE_ST="${C906_FRONT_IVE_ST:-OFF}" \
    -DTIDESENSE_C906_FRONT_ALIGNED_IMAGES="${C906_FRONT_ALIGNED_IMAGES:-OFF}" \
    -DTIDESENSE_C906_FRONT_CPU_TAIL="${C906_FRONT_CPU_TAIL:-OFF}" \
    -DTIDESENSE_C906_FRONT_SPARSE_NMS="${C906_FRONT_SPARSE_NMS:-$front}" \
    -DTIDESENSE_C906_FRONT_LAZY_MASK="${C906_FRONT_LAZY_MASK:-$front}" \
    -DTIDESENSE_C906_FRONT_TPU_BASE_CACHE="${C906_FRONT_TPU_BASE_CACHE:-$front}"
cmake --build "$rtos/build/c906-reload" --target install
name=cvirtos-reload-base
[[ "$probe" == OFF ]] || name=cvirtos-reload-tpu
[[ "$lk" == OFF ]] || name=cvirtos-reload-tpu-lk
[[ "$front" == OFF ]] || name=cvirtos-reload-tpu-front
# Use the actual link output, avoiding timestamp-based install copies.
cp "$rtos/build/c906-reload/main/cvirtos.elf" "$out/$name.elf"
"${riscv}objcopy" -O binary "$out/$name.elf" "$out/$name.bin"
"${arm}gcc" -O2 -Wall -Wextra -static "$repo/scripts/c906/reload.c" -o "$out/c906-reload"
make -C "$repo/linux_5.10" O="$kernel" ARCH=arm64 CROSS_COMPILE="$arm" \
    M="$repo/scripts/c906" \
    KBUILD_EXTRA_SYMBOLS="$repo/osdrv/interdrv/rtos_cmdqu/Module.symvers" modules
cp "$repo/scripts/c906/c906_loader.ko" "$out/"
if [[ "$probe" == ON ]]; then
    "${arm}gcc" -O2 -static -I"$repo/cvi_mpi/include" -I"$repo/cvikernel/include" \
        "$repo/scripts/vio-compare/probes/c906-tpu/host.c" -Wl,--start-group \
        "$repo/cvi_mpi/lib/libsys.a" "$repo/cvi_mpi/lib/libcvikernel-static.a" \
        "$repo/cvi_mpi/lib/libcviruntime-static.a" -Wl,--end-group \
        -lstdc++ -lpthread -lm -lrt -o "$out/c906-tpu-host"
    make -C "$repo/linux_5.10" O="$kernel" ARCH=arm64 CROSS_COMPILE="$arm" \
        M="$repo/scripts/vio-compare/probes/c906-tpu" \
        KBUILD_EXTRA_SYMBOLS="$repo/osdrv/interdrv/rtos_cmdqu/Module.symvers" modules
    cp "$repo/scripts/vio-compare/probes/c906-tpu/c906_tpu_bridge.ko" "$out/"
fi
if [[ "$lk" == ON ]]; then
    C906_LK_ROI_CACHE="$lk_roi_cache" C906_LK_BATCH="$batch" C906_OUT="$out" python3 "$repo/scripts/vio-compare/probes/c906-tpu/build_lk.py"
fi
echo "Built $out/$name.bin, c906-reload, c906_loader.ko"
