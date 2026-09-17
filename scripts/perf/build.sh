#!/bin/bash
# Build a standalone ARM64 perf matching the SDK kernel, with ELF symbols.
set -euo pipefail
sdk_root=$(cd "$(dirname "$0")/../.." && pwd)
work="$sdk_root/install/perf-build"
deps="$work/deps"
prefix="$sdk_root/host-tools/gcc/gcc-linaro-6.3.1-2017.05-x86_64_aarch64-linux-gnu/bin/aarch64-linux-gnu-"
output="$sdk_root/install/soc_sg2002_tidesense_arm64_sd"
jobs=${PERF_JOBS:-8}
archive="$work/elfutils-0.184.tar.bz2"
mkdir -p "$deps" "$work/elfutils-out" "$work/perf-out"
if [ ! -f "$archive" ]; then
    curl -fL --retry 2 https://sourceware.org/elfutils/ftp/0.184/elfutils-0.184.tar.bz2 -o "$archive.tmp"
    mv "$archive.tmp" "$archive"
fi
# Hash from buildroot-2021.05/package/elfutils/elfutils.hash.
printf '%s  %s\n' 5920c354edd6b28bb7e713d674bf6f2221e8ee91b90d582de39b1f45446aea1e7e3d4b6671024c391675b5e7530fe952c1320d730bb625c02d4c647ceb938bc8 "$archive" | sha512sum -c -
if [ ! -d "$work/elfutils-0.184" ]; then
    tar xjf "$archive" -C "$work"
fi
tar xzf "$sdk_root/oss/oss_release_tarball/64bit/zlib.tar.gz" -C "$deps"
(
    cd "$work/elfutils-out"
    if [ ! -f config.status ]; then
        ../elfutils-0.184/configure --host=aarch64-linux-gnu --prefix="$deps" \
        CC="${prefix}gcc" AR="${prefix}ar" RANLIB="${prefix}ranlib" \
        CFLAGS=-O2 CPPFLAGS="-I$deps/include" LDFLAGS="-L$deps/lib" \
        --disable-debuginfod --disable-libdebuginfod --disable-nls --disable-werror \
            --without-bzlib --without-lzma --without-zstd
    fi
    make -j"$jobs" -C lib
    make -j"$jobs" -C libelf
    make -C libelf install
)
make -C "$sdk_root/linux_5.10/tools/perf" -j"$jobs" \
    O="$work/perf-out/" ARCH=arm64 CROSS_COMPILE="$prefix" prefix=/usr \
    EXTRA_CFLAGS="-I$deps/include" LDFLAGS="-static -L$deps/lib" PKG_CONFIG=/bin/false \
    NO_LIBPERL=1 NO_LIBPYTHON=1 NO_SLANG=1 NO_GTK2=1 \
    NO_LIBUNWIND=1 NO_LIBDW_DWARF_UNWIND=1 NO_DWARF=1 NO_LIBBPF=1 \
    NO_LIBAUDIT=1 NO_LIBNUMA=1 NO_LIBCRYPTO=1 NO_LIBCAP=1 \
    NO_LIBBABELTRACE=1 NO_LIBZSTD=1 NO_LIBDEBUGINFOD=1 NO_AUXTRACE=1 \
    NO_JVMTI=1 NO_SDT=1 WERROR=0
install -d "$output/tools/perf" "$output/rootfs/usr/bin"
install -m755 "$work/perf-out/perf" "$output/tools/perf/perf"
"${prefix}strip" "$output/tools/perf/perf"
install -m755 "$output/tools/perf/perf" "$output/rootfs/usr/bin/perf"
install -d "$output/rootfs/usr/share/doc/perf-tip"
install -m644 "$sdk_root/linux_5.10/tools/perf/Documentation/tips.txt" \
    "$output/rootfs/usr/share/doc/perf-tip/tips.txt"
"${prefix}gcc" -O2 -g -fno-omit-frame-pointer -mno-omit-leaf-frame-pointer \
    -fno-optimize-sibling-calls -static "$sdk_root/scripts/perf/smoke.c" \
    -o "$output/tools/perf/perf-smoke"
install -m644 "$work/elfutils-0.184/COPYING-GPLV2" "$output/tools/perf/COPYING-GPLV2"
install -m644 "$work/elfutils-0.184/COPYING-LGPLV3" "$output/tools/perf/COPYING-LGPLV3"
echo "perf installed in $output/tools/perf and rootfs/usr/bin"
