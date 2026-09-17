#!/bin/sh
set -eu
app_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
if [ -d "$app_dir/lib" ]; then
  export LD_LIBRARY_PATH="$app_dir/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
fi
exec "$app_dir/bin/schurvins_stereo" "$@"
