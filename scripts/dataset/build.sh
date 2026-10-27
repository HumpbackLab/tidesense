#!/usr/bin/env bash
set -euo pipefail
here=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
sdk=$(cd "$here/../.." && pwd)
stage="$sdk/install/schurvins-gcc11-static/stage"
build="$sdk/install/tidesense-dataset-build"
out="$sdk/install/soc_sg2002_tidesense_arm64_sd/tidesense-dataset"
[[ -f "$stage/lib/libyaml-cpp.a" ]] || { echo 'Build SDK dependencies first: scripts/schurvins/build.sh' >&2; exit 1; }
deps="$sdk/install/dataset-deps"
mkdir -p "$deps"
archive="$deps/zstd-1.5.7.tar.gz"
if [[ ! -f "$archive" ]]; then
 curl -fL --retry 3 https://github.com/facebook/zstd/releases/download/v1.5.7/zstd-1.5.7.tar.gz -o "$archive.part"
 mv "$archive.part" "$archive"
fi
printf '%s  %s\n' eb33e51f49a15e023950cd7825ca74a4a2b43db8354825ac24fc1b7ee09e6fa3 "$archive" | sha256sum -c -
[[ -d "$deps/zstd-1.5.7" ]] || tar xf "$archive" -C "$deps"
"$sdk/tide_cam/build.sh"
cmake -S "$here" -B "$build" -DCMAKE_BUILD_TYPE=Release \
 -DCMAKE_TOOLCHAIN_FILE="$here/../schurvins/a53-gcc11-toolchain.cmake" -DCMAKE_PREFIX_PATH="$stage"
cmake --build "$build" -j"${JOBS:-4}"
mkdir -p "$out/bin"
cross="$sdk/host-tools/gcc/arm-gnu-toolchain-11.3.rel1-x86_64-aarch64-none-linux-gnu/bin/aarch64-none-linux-gnu-"
"${cross}strip" --strip-unneeded -o "$out/bin/tidesense_record" "$build/tidesense_record"
rm -f "$out/bin/schurvins_camera" "$out/bin/fsin.sh"
cp "$here/../schurvins/prepare-imu.sh" "$out/bin/"
mkdir -p "$out/licenses"
cp "$deps/zstd-1.5.7/LICENSE" "$out/licenses/zstd-BSD.txt"
cp "$sdk/install/schurvins-build/deps/opencv-3.4.20/LICENSE" "$out/licenses/OpenCV-LICENSE"
cp "$sdk/install/schurvins-build/deps/yaml-cpp/LICENSE" "$out/licenses/yaml-cpp-LICENSE"
cp "$here/run.sh" "$here/export.py" "$here/README.md" "$here/VALIDATION.md" "$out/"
tar -C "$(dirname "$out")" -cf "$out-a53.tar" "$(basename "$out")"
echo "$out-a53.tar"
