#!/bin/sh
# TideSense PWR_GPIO0 / PWM8: hardware-timed pulses shared by both OV9281s.
set -eu
state=/run/tidesense-fsin
lock=/run/tidesense-fsin.lock
mkdir "$lock" 2>/dev/null || { echo 'FSIN control is busy' >&2; exit 1; }
trap 'rmdir "$lock"' EXIT
fail() { echo "$*" >&2; exit 1; }
route() {
 value=$(busybox devmem 0x030010a4 32)
 case $((value & 7)) in 0|4) ;; *) fail 'PWR_GPIO0 is assigned to another peripheral' ;; esac
 busybox devmem 0x030010a4 32 "$(((value & ~7) | $1))"
}
locate_pwm() {
 pwmchip=
 for chip in /sys/class/pwm/pwmchip*; do
  case "$(readlink -f "$chip")" in */3062000.pwm/pwm/*) pwmchip=$chip ;; esac
 done
 [ -n "$pwmchip" ] || fail 'PWM8 controller 3062000.pwm is unavailable'
 pwm=$pwmchip/pwm0
}
prepare() {
 [ -d /sys/module/cv181x_pwm ] || busybox insmod /mnt/system/ko/cv181x_pwm.ko
 locate_pwm
 if [ -d "$pwm" ] && [ ! -d "$state" ]; then fail 'PWM8 is already exported by another user'; fi
 if [ -f "$pwm/enable" ] && [ "$(cat "$pwm/enable")" != 0 ]; then fail 'Stop FSIN before prepare'; fi
 gpio=
 for chip in /sys/class/gpio/gpiochip*; do
  [ "$(cat "$chip/label")" = 5021000.gpio ] && gpio=$(cat "$chip/base")
 done
 [ -n "$gpio" ] || fail 'PWR GPIO bank not found'
 if [ -d "/sys/class/gpio/gpio$gpio" ] && [ ! -d "$state" ]; then fail 'PWR_GPIO0 is already exported'; fi
 [ -d "/sys/class/gpio/gpio$gpio" ] || echo "$gpio" > /sys/class/gpio/export
 echo low > "/sys/class/gpio/gpio$gpio/direction"
 route 0
 mkdir -p "$state"
 [ -d "$pwm" ] || echo 0 > "$pwmchip/export"
 echo 50000000 > "$pwm/period"
 echo 0 > "$pwm/enable"
 echo 100000 > "$pwm/duty_cycle"
 echo normal > "$pwm/polarity"
 echo 0 > "$pwm/count_mode"
 echo 'FSIN prepared: PWR_GPIO0 low, PWM8 stopped; pulse width 100 us'
}
case "${1:-}" in
 prepare) [ "$#" -eq 1 ] || fail 'prepare takes no arguments'; prepare ;;
 start|train)
  action=$1
  [ "$#" -le 3 ] || fail 'Too many arguments'
  hz=${2:-20}
  case "$hz" in ''|0*|*[!0-9]*) fail 'Frequency must be an integer from 1 to 20 Hz' ;; esac
  [ "$hz" -ge 1 ] && [ "$hz" -le 20 ] || fail 'Frequency must be from 1 to 20 Hz'
  [ -d "$state" ] || prepare
  locate_pwm
  [ "$(cat "$pwm/enable")" = 0 ] || fail 'Stop FSIN before starting a new pulse train'
  echo $((1000000000 / hz)) > "$pwm/period"
  echo 100000 > "$pwm/duty_cycle"
  if [ "$action" = train ]; then
   count=${3:-}
   case "$count" in ''|0*|*[!0-9]*) fail 'Usage: fsin.sh train HZ COUNT' ;; esac
   [ "$count" -ge 1 ] && [ "$count" -le 10000000 ] || fail 'COUNT must be 1..10000000'
   echo 1 > "$pwm/count_mode"
   echo "$count" > "$pwm/out_count"
  else
   [ "$#" -le 2 ] || fail 'Usage: fsin.sh start [HZ]'
   echo 0 > "$pwm/count_mode"
  fi
  route 4
  echo 1 > "$pwm/enable"
  echo "FSIN $action: PWM8, $hz Hz, 100 us high${count:+, $count pulses}"
  ;;
 stop)
  [ -d "$state" ] || fail 'FSIN has not been prepared'
  locate_pwm
  echo 0 > "$pwm/enable"
  route 0
  echo 'FSIN stopped: PWR_GPIO0 low'
  ;;
 status)
  locate_pwm
  for attr in enable period duty_cycle count_mode; do
   printf '%s=' "$attr"; cat "$pwm/$attr"
  done
  if [ "$(cat "$pwm/count_mode")" = 1 ]; then printf 'out_count='; cat "$pwm/out_count"; fi
  ;;
 *) fail 'Usage: fsin.sh prepare | start [HZ] | train HZ COUNT | stop | status' ;;
esac
