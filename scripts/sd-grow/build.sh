#!/bin/bash
set -euo pipefail
here=$(cd "$(dirname "$0")" && pwd)
sdk=$(cd "$here/../.." && pwd)
work="$sdk/install/tidesense-grow"
cross="${CROSS_COMPILE:-$sdk/host-tools/gcc/arm-gnu-toolchain-11.3.rel1-x86_64-aarch64-none-linux-gnu/bin/aarch64-none-linux-gnu-}"
mkdir -p "$work"
archive="$work/e2fsprogs-1.47.2.tar.xz"
if [ ! -f "$archive" ]; then
    curl -fL --retry 3 https://www.kernel.org/pub/linux/kernel/people/tytso/e2fsprogs/v1.47.2/e2fsprogs-1.47.2.tar.xz -o "$archive.part"
    mv "$archive.part" "$archive"
fi
printf '%s  %s\n' 08242e64ca0e8194d9c1caad49762b19209a06318199b63ce74ae4ef2d74e63c "$archive" | sha256sum -c -
[ -d "$work/e2fsprogs-1.47.2" ] || tar xf "$archive" -C "$work"
mkdir -p "$work/cross-build"
cd "$work/cross-build"
if [ ! -f Makefile ]; then
    ../e2fsprogs-1.47.2/configure --host=aarch64-linux-gnu CC="${cross}gcc" \
      LDFLAGS=-static --disable-nls --disable-fuse2fs --disable-elf-shlibs \
      --disable-uuidd --enable-libblkid --enable-libuuid
fi
make -j"${JOBS:-8}" libs
make -C resize -j"${JOBS:-8}" resize2fs
make -C e2fsck -j"${JOBS:-8}" e2fsck
cp resize/resize2fs "$work/resize2fs"
cp e2fsck/e2fsck "$work/e2fsck"
"${cross}gcc" -O2 -static -Wall -Wextra -Werror "$here/grow-data.c" -o "$work/tidesense-grow-data"
"${cross}strip" "$work/resize2fs" "$work/e2fsck" "$work/tidesense-grow-data"
