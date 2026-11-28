#!/bin/sh
# Add UVC to the existing SDK RNDIS gadget. Never replace the RNDIS function.
set -eu
# Dropbear non-login sessions omit /sbin; ifconfig and SDK devmem helpers live there.
PATH=/sbin:/usr/sbin:/bin:/usr/bin${PATH:+:$PATH}
export PATH
base=$(CDPATH='' cd -- "$(dirname -- "$0")" && pwd)
state=/run/tidesense-uvc
gadget=/tmp/usb/usb_gadget/cvitek
func=$gadget/functions/uvc.tidesense
config=$gadget/configs/c.1
log=$base/uvc.log
vi_module=/mnt/system/ko/cv181x_vi.ko
vi_sysfs=/sys/module/cv181x_vi
fail() { echo "TideSense UVC: $*" >&2; exit 1; }
read_pid() {
 [ -f "$state/pid" ] || return 1
 pid=$(cat "$state/pid")
 case "$pid" in ''|*[!0-9]*) return 1 ;; esac
 [ -r "/proc/$pid/comm" ] && [ "$(cat "/proc/$pid/comm")" = tidesense_uvc ]
}
remove_uvc() {
 [ -L "$config/uvc.tidesense" ] && rm "$config/uvc.tidesense"
 if [ -d "$func" ]; then
  for p in streaming/class/fs/h streaming/class/hs/h streaming/class/ss/h \
           control/class/fs/h control/class/ss/h streaming/header/h/m; do
   [ ! -L "$func/$p" ] || rm "$func/$p"
  done
  [ ! -d "$func/streaming/mjpeg/m/400p" ] || rmdir "$func/streaming/mjpeg/m/400p"
  [ ! -d "$func/streaming/mjpeg/m" ] || rmdir "$func/streaming/mjpeg/m"
  [ ! -d "$func/streaming/header/h" ] || rmdir "$func/streaming/header/h"
  [ ! -d "$func/control/header/h" ] || rmdir "$func/control/header/h"
  rmdir "$func"
 fi
}
restore() {
 if read_pid; then
  kill "$pid"
  n=0
  while kill -0 "$pid" 2>/dev/null; do
   n=$((n+1)); [ "$n" -lt 15 ] || fail "capture process did not stop; inspect $log"
   sleep 1
  done
 fi
 [ ! -d "$state" ] || rm -f "$state/pid" "$state/ready"
 if [ -f "$state/switched" ] && [ -n "$(cat "$gadget/UDC")" ]; then echo '' > "$gadget/UDC"; fi
 remove_uvc
 if [ -f "$state/switched" ]; then
  for p in idProduct bDeviceClass bDeviceSubClass bDeviceProtocol; do
   cat "$state/$p" > "$gadget/$p"
  done
  cat "$state/product" > "$gadget/strings/0x409/product"
  # Retain the inbox RNDIS6 match across both modes. Restoring the SDK's empty
  # sub-ID makes Windows discard the driver selected for the composite mode.
  echo 5162001 > "$gadget/functions/rndis.usb0/os_desc/interface.rndis/sub_compatible_id"
  cat "$state/UDC" > "$gadget/UDC"
  ifconfig usb0 192.168.123.99 netmask 255.255.255.0 up
 fi
 if [ -f "$state/restore_vi" ]; then
  if [ -d "$vi_sysfs" ]; then rmmod cv181x_vi || fail "cannot release front-end VI"; fi
  insmod "$vi_module" || fail "cannot restore the ISP VI module"
 fi
 rm -rf "$state"
}
start_impl() {
 mode=$1
 sensor_sync=${TIDESENSE_UVC_SENSOR_SYNC:-0}
 if [ "$mode" != 400p40 ]; then sensor_sync=1; fi
 mkdir "$state" || fail "already active or switching; use status/stop"
 # shellcheck disable=SC2154
 trap 'rc=$?; trap - EXIT; if [ "$rc" -ne 0 ]; then restore; fi; exit "$rc"' EXIT
 if [ ! -d "$gadget" ] || [ ! -d "$config/rndis.usb0" ]; then fail "expected SDK RNDIS gadget (rndis.usb0)"; fi
 for f in "$gadget"/functions/*; do
  [ "${f##*/}" = rndis.usb0 ] || fail "unexpected gadget function: $f"
 done
 [ -x "$base/bin/tidesense_uvc" ] || fail "missing capture binary"
 descriptor=$("$base/bin/tidesense_uvc" --describe "$mode") || fail "unknown mode: $mode"
 read -r width height interval usb_interval max_frame <<EOF
$descriptor
EOF
 printf '%s\n' "$mode" > "$state/mode"
 fe_only=$(cat "$vi_sysfs/parameters/tidesense_fe_only" 2>/dev/null || :)
 maxpacket=3072
 case "$mode" in
  480p180|400p210) maxpacket=1024 ;;
  *) [ "$fe_only" != Y ] || fail "load the VI driver with the ISP path enabled" ;;
 esac
 if [ ! -r /sys/module/dwc2/parameters/gadget_desc_dma ] ||
    [ "$(cat /sys/module/dwc2/parameters/gadget_desc_dma)" != N ]; then
  fail "load the matching dwc2.ko with gadget_desc_dma=0 before starting UVC"
 fi
 # Verify kernel capability before disconnecting the debug connection.
 capable=0
 for d in /sys/bus/iio/devices/iio:device*; do
  [ -f "$d/name" ] || continue
  [ "$(cat "$d/name")" = sc7u22 ] || continue
  [ -f "$d/in_accel_sampling_frequency_available" ] || continue
  [ "$sensor_sync" = 0 ] || [ -w "$d/events/in_count0_change_rising_en" ] || continue
  case " $(cat "$d/in_accel_sampling_frequency_available") " in *' 400 '*) capable=1 ;; esac
 done
 [ "$capable" = 1 ] || fail "install the matching 200/400Hz and FSIN kernel first"
 if [ "$maxpacket" = 1024 ] && [ "$fe_only" != Y ]; then
  [ "$fe_only" = N ] && [ -f "$vi_module" ] ||
   fail "install the VI driver with front-end RAW8 support first"
  [ "$(cat "$vi_sysfs/refcnt")" = 0 ] || fail "VI is in use; stop other camera programs"
  rmmod cv181x_vi || fail "cannot release the ISP VI module"
  touch "$state/restore_vi"
  insmod "$vi_module" tidesense_fe_only=1 || fail "cannot load front-end VI"
 fi
 for p in idProduct bDeviceClass bDeviceSubClass bDeviceProtocol UDC; do cat "$gadget/$p" > "$state/$p"; done
 cat "$gadget/strings/0x409/product" > "$state/product"
 [ -s "$state/UDC" ] || fail "RNDIS gadget must already be bound"
 touch "$state/saved"
 # Prepare and validate every attribute while RNDIS is still connected.
 mkdir "$func"
 # Keep RNDIS enumerated even if the video producer fails or restarts.
 echo 1 > "$func/keep_connected"
 echo "$usb_interval" > "$func/streaming_interval"
 echo "$maxpacket" > "$func/streaming_maxpacket"
 mkdir "$func/streaming/mjpeg/m"
 mkdir "$func/streaming/mjpeg/m/400p"
 frame=$func/streaming/mjpeg/m/400p
 echo "$width" > "$frame/wWidth"; echo "$height" > "$frame/wHeight"
 echo "$max_frame" > "$frame/dwMaxVideoFrameBufferSize"
 echo "$interval" > "$frame/dwFrameInterval"
 echo "$interval" > "$frame/dwDefaultFrameInterval"
 echo 1000000 > "$frame/dwMinBitRate"
 echo "$((max_frame * 8 * 10000000 / interval))" > "$frame/dwMaxBitRate"
 mkdir "$func/streaming/header/h"
 ln -s "$func/streaming/mjpeg/m" "$func/streaming/header/h/m"
 # High-speed descriptors only. The full-speed bus has insufficient bandwidth.
 ln -s "$func/streaming/header/h" "$func/streaming/class/hs/h"
 mkdir "$func/control/header/h"
 # This SDK's processing-unit descriptor has the UVC 1.0 (11-byte) layout.
 echo 0x0100 > "$func/control/header/h/bcdUVC"
 echo 1000000 > "$func/control/header/h/dwClockFrequency"
 ln -s "$func/control/header/h" "$func/control/class/fs/h"
 ln -s "$func/control/header/h" "$func/control/class/ss/h"
 # Only streaming negotiation is implemented; advertise no camera/ISP controls.
 echo 0 > "$func/control/terminal/camera/default/bmControls"
 echo 0 > "$func/control/processing/default/bmControls"
 touch "$state/switched"
 echo '' > "$gadget/UDC"
 # Keep the existing RNDIS identity/driver and its host network configuration.
 echo 0xef > "$gadget/bDeviceClass"
 echo 2 > "$gadget/bDeviceSubClass"
 echo 1 > "$gadget/bDeviceProtocol"
 echo 'TideSense Stereo + IMU' > "$gadget/strings/0x409/product"
 echo 5162001 > "$gadget/functions/rndis.usb0/os_desc/interface.rndis/sub_compatible_id"
 ln -s "$func" "$config/uvc.tidesense"
 cat "$state/UDC" > "$gadget/UDC"
 ifconfig usb0 192.168.123.99 netmask 255.255.255.0 up
 video=
 for d in /sys/class/video4linux/video*; do
  [ -f "$d/function_name" ] || continue
  if [ "$(cat "$d/function_name")" = uvc.tidesense ]; then video=/dev/${d##*/}; fi
 done
 [ -n "$video" ] || fail "UVC output node missing"
 set -- --mode "$mode" --ini "$base/sensor_cfg.ini" --device "$video" \
  --ready-file "$state/ready" --maxpacket "$maxpacket"
 if [ "$sensor_sync" = 1 ]; then set -- "$@" --master right; fi
 "$base/bin/tidesense_uvc" "$@" &
 producer=$!
 echo "$producer" > "$state/pid"
 n=0
 while [ ! -f "$state/ready" ]; do
  kill -0 "$producer" 2>/dev/null || fail "capture exited; see $log"
  n=$((n+1)); [ "$n" -lt 30 ] || fail "capture startup timeout"
  sleep 1
 done
 echo "UVC $mode + RNDIS ready: $video, SSH 192.168.123.99; log $log"
 trap - EXIT
}
case "${1:-status}" in
 start|stop)
  # USB re-enumeration drops SSH. Keep the entire switch/rollback detached.
  action=$1
  if [ "$action" = start ]; then
   [ "$#" -le 2 ] || fail "Usage: run.sh start [400p40|800p60|480p180|400p210]"
   [ ! -d "$state" ] || fail "already active; use status/stop"
   set -- "$action" "${2:-400p40}"
   "$base/bin/tidesense_uvc" --describe "$2" >/dev/null || fail "unknown mode: $2"
  else
   [ "$#" -eq 1 ] || fail "stop takes no arguments"
  fi
  mkdir /run/tidesense-uvc-switch.lock 2>/dev/null || fail "switch already running"
  shift
  nohup "$base/run.sh" "_$action" "$@" </dev/null >>"$log" 2>&1 &
  echo "USB $action scheduled; connection may reconnect. Log: $log"
  ;;
 _start|_stop)
  trap 'rmdir /run/tidesense-uvc-switch.lock' EXIT
  if [ "$1" = _start ]; then
   # start_impl has a rollback trap; run in a subshell to retain this lock trap.
   (start_impl "${2:-400p40}")
  else
   [ -d "$state" ] || fail "UVC mode is not active"
   restore
   echo 'RNDIS-only mode restored'
  fi
  ;;
 status)
  if read_pid; then echo "UVC process $pid; RNDIS 192.168.123.99"; else echo 'UVC process stopped'; fi
  [ ! -d /run/tidesense-uvc-switch.lock ] || echo 'USB switch in progress'
  [ ! -f "$state/ready" ] || echo "UVC ready: $(cat "$state/mode" 2>/dev/null || echo 400p40)"
  ;;
 capture)
  [ "$#" -eq 3 ] || [ "$#" -eq 4 ] || fail "Usage: run.sh capture OUTPUT.mjpg FRAME_COUNT [MODE]"
  exec "$base/bin/tidesense_uvc" --mode "${4:-400p40}" --ini "$base/sensor_cfg.ini" --capture "$2" --frames "$3"
  ;;
 *) fail "Usage: $0 start [MODE] | stop | status | capture OUTPUT.mjpg FRAME_COUNT [MODE]" ;;
esac
