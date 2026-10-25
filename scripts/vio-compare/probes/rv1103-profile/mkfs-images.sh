#!/usr/bin/env bash
# Run under fakeroot. SDK's static mkfs.ext4 cannot observe fakeroot ownership.
set -euo pipefail
staging=$1
images=$2
chown -hR 0:0 "$staging"
for partition in rootfs oem userdata; do
    case "$partition" in rootfs) size=6144M ;; oem) size=512M ;; userdata) size=256M ;; esac
    /usr/sbin/mkfs.ext4 -F -d "$staging/$partition" -r 1 -N 0 -m 5 -L '' \
        -O '^64bit,^huge_file' -E root_owner=0:0,lazy_itable_init=0,lazy_journal_init=0 \
        "$images/$partition.img" "$size"
    /usr/sbin/resize2fs -M "$images/$partition.img"
    /usr/sbin/e2fsck -fn "$images/$partition.img"
done
