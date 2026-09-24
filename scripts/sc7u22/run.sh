#!/bin/sh
set -eu
# Find the physical SPI2 controller regardless of Linux bus numbering.
device=
for node in /sys/bus/spi/devices/spi*; do
    [ -f "$node/modalias" ] || continue
    case "$(cat "$node/modalias")" in *sc7u22*) ;; *) continue ;; esac
    case "$(readlink -f "$node")" in */41a0000.spi2/*) device=$node; break ;; esac
done
[ -n "$device" ] || { echo "SC7U22 SPI2 device missing; install the TideSense IMU device tree." >&2; exit 1; }
name=${device##*/}
if [ -L "$device/driver" ]; then
    [ "$(basename "$(readlink -f "$device/driver")")" = spidev ] || {
        echo "$name is owned by another driver; stop its application before testing." >&2; exit 1;
    }
else
    echo spidev > "$device/driver_override"
    echo "$name" > /sys/bus/spi/drivers/spidev/bind
fi
node=/dev/spidev${name#spi}
[ -c "$node" ] || { echo "Missing $node" >&2; exit 1; }
exec "$(dirname "$0")/sc7u22_test" "$node" "${1:-10}" "${2:-1000000}"
