#!/bin/sh
# Check the SDK implementation under a separate symbol, avoiding libc interposition.
set -eu
repo=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT INT TERM
cat > "$tmp/test.c" <<'EOF'
#include <assert.h>
#include <stddef.h>
extern void *sdk_test_memset(void *, int, unsigned long);
int main(void) {
    unsigned char data[65];
    assert(sdk_test_memset(data, 0x1ab, sizeof(data)) == data);
    for (size_t i = 0; i < sizeof(data); ++i) assert(data[i] == 0xab);
    assert(sdk_test_memset(data, 0, 0) == data);
    assert(sdk_test_memset(data + 1, -1, 63) == data + 1);
    assert(data[0] == 0xab && data[64] == 0xab);
    for (size_t i = 1; i < 64; ++i) assert(data[i] == 255);
    return 0;
}
EOF
${CC:-cc} -O3 -fno-builtin -Dmemset=sdk_test_memset -c \
  "$repo/freertos/cvitek/common/src/riscv64/memset.c" -o "$tmp/memset.o"
${CC:-cc} -O2 "$tmp/test.c" "$tmp/memset.o" -o "$tmp/test"
"$tmp/test"
echo 'SDK memset return value, range and zero-length checks passed'
