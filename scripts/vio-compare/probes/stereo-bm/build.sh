#!/usr/bin/env bash
set -euo pipefail
here=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
sdk=$(cd -- "$here/../../../.." && pwd)
out=${1:-"$sdk/install/stereo-bm-a53"}
stage="$sdk/install/schurvins-gcc11-static/stage"
cross="$sdk/host-tools/gcc/arm-gnu-toolchain-11.3.rel1-x86_64-aarch64-none-linux-gnu/bin/aarch64-none-linux-gnu-g++"
mkdir -p "$out"
"$cross" -std=c++14 -O3 -DNDEBUG -mcpu=cortex-a53 -ffunction-sections -fdata-sections \
  -I"$stage/include" "$here/bench.cpp" -static -Wl,--gc-sections -o "$out/stereo_bm" \
  -L"$stage/lib" -L"$stage/share/OpenCV/3rdparty/lib" -Wl,--start-group \
  -lopencv_calib3d -lopencv_features2d -lopencv_flann -lopencv_imgcodecs \
  -lopencv_imgproc -lopencv_core -llibjpeg-turbo -llibpng -lzlib -ltegra_hal \
  -Wl,--end-group -ldl -lm -lpthread -lrt
"${cross%g++}strip" "$out/stereo_bm"
