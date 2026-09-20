#!/bin/sh
set -eu
# Kernel owns SPI and INT1; never override or unbind its driver.
for node in /sys/bus/iio/devices/iio:device*; do
    [ -f "$node/name" ] || continue
    [ "$(cat "$node/name")" = sc7u22 ] || continue
    case "$(readlink -f "$node")" in */41a0000.spi2/*) ;; *) continue ;; esac
    device=/dev/${node##*/}
    [ -c "$device" ] || { echo "Missing $device" >&2; exit 1; }
    printf '%s\n' "$device"
    exit 0
done
echo "SC7U22 IIO device missing; install the INT1-enabled TideSense kernel/device tree." >&2
exit 1
