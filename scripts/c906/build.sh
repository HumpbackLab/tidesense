#!/usr/bin/env bash
set -euo pipefail
repo=$(cd "$(dirname "$0")/../.." && pwd)
out=${C906_OUT:-"$repo/install/c906-reload"}
if [[ $# != 0 ]]; then
    echo "usage: $0" >&2
    exit 2
fi
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
cmake --build "$rtos/build/kernel" --target install
cmake -G Ninja -S "$rtos/task" -B "$rtos/build/c906-reload" \
    -DCMAKE_TOOLCHAIN_FILE="$rtos/scripts/toolchain-riscv64-elf.cmake" \
    -DTOP_DIR="$rtos" -DBUILD_ENV_PATH="$repo/build" -DCHIP=cv181x \
    -DRUN_ARCH=riscv64 -DRUN_TYPE=CVIRTOS -DBOARD_FPGA=n \
    -DTIDESENSE_C906_RELOAD=ON
cmake --build "$rtos/build/c906-reload" --target install
name=cvirtos-reload-base
# Use the actual link output, avoiding timestamp-based install copies.
cp "$rtos/build/c906-reload/main/cvirtos.elf" "$out/$name.elf"
"${riscv}objcopy" -O binary "$out/$name.elf" "$out/$name.bin"
"${arm}gcc" -O2 -Wall -Wextra -static "$repo/scripts/c906/reload.c" -o "$out/c906-reload"
make -C "$repo/linux_5.10" O="$kernel" ARCH=arm64 CROSS_COMPILE="$arm" \
    M="$repo/scripts/c906" \
    KBUILD_EXTRA_SYMBOLS="$repo/osdrv/interdrv/rtos_cmdqu/Module.symvers" modules
cp "$repo/scripts/c906/c906_loader.ko" "$out/"
echo "Built $out/$name.bin, c906-reload, c906_loader.ko"
