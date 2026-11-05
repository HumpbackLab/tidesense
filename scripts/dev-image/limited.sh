#!/usr/bin/env bash
# Bound the complete build/check command, including Podman and filesystem cache.
set -euo pipefail
[[ $# -gt 0 ]] || { echo "Usage: $0 COMMAND [ARGUMENTS...]" >&2; exit 2; }
if [[ ${TIDESENSE_IN_MEMORY_SCOPE:-0} != 1 ]]; then
  exec systemd-run --user --scope --collect --unit="tidesense-dev-build-$$" \
    -p "MemoryHigh=${TIDESENSE_BUILD_MEMORY_HIGH:-2G}" \
    -p "MemoryMax=${TIDESENSE_BUILD_MEMORY_MAX:-3G}" \
    -p "MemorySwapMax=${TIDESENSE_BUILD_SWAP_MAX:-1G}" \
    -p CPUQuota=100% \
    env TIDESENSE_IN_MEMORY_SCOPE=1 "$0" "$@"
fi

scope=$(sed -n 's/^0:://p' /proc/self/cgroup)
control="/sys/fs/cgroup$scope"
[[ -f "$control/memory.max" && $(cat "$control/memory.max") != max ]] || {
  echo 'A working systemd user scope and cgroup v2 memory controller are required.' >&2
  exit 1
}
echo "Build memory limit: $(cat "$control/memory.max") bytes; swap: $(cat "$control/memory.swap.max") bytes"

work=$(mktemp -d "${TMPDIR:-/tmp}/tidesense-limited-build.XXXXXXXX")
export TIDESENSE_PODMAN_REAL
TIDESENSE_PODMAN_REAL=$(command -v podman)
cat > "$work/podman" <<'PODMAN'
#!/usr/bin/env bash
set -euo pipefail
# These are the global options used by the SDK helpers. Keep rootless Podman in
# the caller's scope instead of letting it create a separate systemd unit.
while [[ ${1:-} == --cgroup-manager=* ]]; do shift; done
command=${1:-}
[[ -z $command ]] || shift
case "$command" in
  run|create)
    exec "$TIDESENSE_PODMAN_REAL" --cgroup-manager=cgroupfs "$command" --cgroups=disabled "$@"
    ;;
  build)
    args=()
    for argument in "$@"; do
      [[ $argument == --isolation=* ]] || args+=("$argument")
    done
    # chroot avoids moving RUN processes into a sibling container cgroup.
    exec "$TIDESENSE_PODMAN_REAL" --cgroup-manager=cgroupfs build --isolation=chroot "${args[@]}"
    ;;
  *) exec "$TIDESENSE_PODMAN_REAL" --cgroup-manager=cgroupfs ${command:+"$command"} "$@" ;;
esac
PODMAN
chmod 755 "$work/podman"
export PATH="$work:$PATH"
export KALIBR_BUILD_JOBS=1 GOMAXPROCS=1 GOMEMLIMIT=512MiB
cleanup() {
  limited_exit=$?
  echo "Build memory peak: $(cat "$control/memory.peak") bytes"
  cat "$control/memory.events"
  rm -rf -- "$work"
  exit "$limited_exit"
}
trap cleanup EXIT
"$@"
