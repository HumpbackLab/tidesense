#!/bin/sh
# Exclusively own TPU for one process; restore normal firmware/driver on exit.
set -eu
if [ "$#" -eq 0 ]; then echo 'Usage: with-tpu.sh COMMAND [ARGS...]' >&2; exit 2; fi
if [ "${OPENVINS_TPU_LK:-1}" = 0 ]; then exec "$@"; fi
export OPENVINS_TPU_LK=1
tools=${C906_TOOLS_DIR:-/mnt/data/c906-reload}
if awk '$1=="c906_tpu_bridge" {found=1} END {exit !found}' /proc/modules; then
  echo 'TPU bridge already loaded; stop its owner and unload it before using with-tpu.sh.' >&2
  exit 1
fi
linux_loaded=0
if awk '$1=="cv181x_tpu" {found=1} END {exit !found}' /proc/modules; then linux_loaded=1; fi
want_ive=0
if [ "${OPENVINS_TPU_IVE:-0}" = 1 ] || [ "${OPENVINS_TPU_IVE_HIST:-0}" = 1 ] || [ "${OPENVINS_TPU_IVE_ST:-0}" = 1 ]; then want_ive=1; fi
linux_ive_loaded=0
if [ "$want_ive" = 1 ] && awk '$1=="cv181x_ive" {found=1} END {exit !found}' /proc/modules; then linux_ive_loaded=1; fi
changed=0
owned=0
child=
cleanup() {
  if [ "$owned" = 1 ]; then /sbin/rmmod c906_tpu_bridge || true; owned=0; fi
  if [ "$changed" = 1 ]; then "$tools/c906-reload" load "$tools/cvirtos-reload-base.bin" || true; changed=0; fi
  if [ "$linux_loaded" = 1 ] && ! awk '$1=="cv181x_tpu" {found=1} END {exit !found}' /proc/modules; then
    /sbin/insmod /mnt/system/ko/cv181x_tpu.ko || true
  fi
  if [ "$linux_ive_loaded" = 1 ] && ! awk '$1=="cv181x_ive" {found=1} END {exit !found}' /proc/modules; then
    /sbin/insmod /mnt/system/ko/cv181x_ive.ko || true
  fi
}
interrupted() {
  if [ -n "$child" ]; then kill -TERM "$child" 2>/dev/null || true; wait "$child" || true; child=; fi
  exit 130
}
trap cleanup EXIT
trap interrupted INT TERM
[ -e /dev/c906-loader ] || /sbin/insmod "$tools/c906_loader.ko"
firmware="$tools/cvirtos-reload-tpu-lk.bin"
if [ "${OPENVINS_TPU_FRONTEND:-0}" != 0 ]; then firmware="$tools/cvirtos-reload-tpu-front.bin"; fi
[ -r "$firmware" ] || { echo "Missing C906 firmware: $firmware" >&2; exit 1; }
"$tools/c906-reload" load "$firmware"
changed=1
if [ "$linux_loaded" = 1 ]; then /sbin/rmmod cv181x_tpu; fi
if [ "$linux_ive_loaded" = 1 ]; then /sbin/rmmod cv181x_ive; fi
owned=1
if [ "$want_ive" = 1 ]; then
  /sbin/insmod "$tools/c906_tpu_bridge.ko" ive=1
else
  /sbin/insmod "$tools/c906_tpu_bridge.ko"
fi
"$@" &
child=$!
set +e
wait "$child"
result=$?
child=
exit "$result"
