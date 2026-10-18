#!/usr/bin/env bash
set -euo pipefail
here=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
sdk=$(cd -- "$here/../.." && pwd)
source "$here/sources.lock"
# GCC 11 full-static is the supported build; accept the previous explicit flag.
case "${1:-}" in
  ""|--gcc11-static) ;;
  *) echo "Usage: $0 [--gcc11-static]" >&2; exit 2 ;;
esac
work="$sdk/install/schurvins-gcc11-static"
stage="$work/stage"
out="$sdk/install/soc_sg2002_tidesense_arm64_sd/schurvins-gcc11-static"
micro_ros=${SCHUR_WITH_MICRO_ROS:-OFF}
case "$micro_ros" in
  OFF) core="$work/core" ;;
  ON) core="$work/core-micro-ros"; out="$out-micro-ros" ;;
  *) echo 'SCHUR_WITH_MICRO_ROS must be ON or OFF' >&2; exit 2 ;;
esac
deps="$sdk/install/schurvins-build/deps"
jobs=${JOBS:-4}
toolchain="$here/a53-gcc11-toolchain.cmake"
cross="$sdk/host-tools/gcc/arm-gnu-toolchain-11.3.rel1-x86_64-aarch64-none-linux-gnu/bin/aarch64-none-linux-gnu-"
for command in git curl cmake make tar sha256sum; do command -v "$command" >/dev/null; done
[[ -x "${cross}g++" ]] || { echo "Missing SDK AArch64 compiler" >&2; exit 1; }
mkdir -p "$deps" "$stage/include" "$work/sysroot"
fetch_git() {
  local url=$1 dir=$2 rev=$3
  if [[ ! -d "$dir/.git" ]]; then
    git init "$dir"
    git -C "$dir" remote add origin "$url"
    git -C "$dir" fetch --depth 1 origin "$rev"
    git -C "$dir" checkout --detach FETCH_HEAD
  fi
  if [[ ${4:-} == allow-local-commits ]] && git -C "$dir" merge-base --is-ancestor "$rev" HEAD; then
    return
  fi
  [[ $(git -C "$dir" rev-parse HEAD) == "$rev" ]] || {
    echo "Unexpected source revision in $dir; see sources.lock (checkout left untouched)" >&2; exit 1;
  }
}
verify() { printf '%s  %s\n' "$1" "$2" | sha256sum -c -; }
fetch_git https://github.com/bytedance/SchurVINS.git "$sdk/SchurVINS" "$SCHUR_REV" allow-local-commits
fetch_git https://github.com/ethz-asl/minkindr.git "$deps/minkindr" "$MINKINDR_REV"
fetch_git https://github.com/uzh-rpg/fast_neon.git "$deps/fast_neon" "$FAST_REV"
fetch_git https://github.com/jbeder/yaml-cpp.git "$deps/yaml-cpp" "$YAML_REV"
for patch in "$here"/patches/*.patch; do
  if git -C "$sdk/SchurVINS" apply --reverse --check "$patch" 2>/dev/null; then
    echo "Already applied: $patch"
  else
    git -C "$sdk/SchurVINS" apply --check "$patch"
    git -C "$sdk/SchurVINS" apply "$patch"
  fi
done
archive="$deps/opencv-$OPENCV_VERSION.tar.gz"
if [[ ! -f "$archive" ]]; then
  curl -fL --retry 3 "https://codeload.github.com/opencv/opencv/tar.gz/refs/tags/$OPENCV_VERSION" -o "$archive.part"
  verify "$OPENCV_SHA256" "$archive.part"
  mv "$archive.part" "$archive"
fi
verify "$OPENCV_SHA256" "$archive"
[[ -d "$deps/opencv-$OPENCV_VERSION" ]] || tar xf "$archive" -C "$deps"
verify "$EIGEN_SHA256" "$sdk/oss/oss_release_tarball/64bit/eigen.tar.gz"
tar xf "$sdk/oss/oss_release_tarball/64bit/eigen.tar.gz" -C "$work/sysroot"
# Only Boost headers are used, never host libraries or /usr/include itself.
# Ubuntu/Debian: apt install libboost1.74-dev
boost_headers=${BOOST_HEADERS:-/usr/include/boost}
if [[ ! -f "$stage/include/boost/version.hpp" ]]; then
  [[ -f "$boost_headers/version.hpp" ]] || { echo "Install Boost 1.74 headers or set BOOST_HEADERS" >&2; exit 1; }
  cp -a "$boost_headers" "$stage/include/boost"
fi
grep -q '^#define BOOST_VERSION 107400$' "$stage/include/boost/version.hpp" || {
  echo "This build is verified with Boost 1.74 headers; set BOOST_HEADERS accordingly" >&2; exit 1;
}
common=(-DCMAKE_TOOLCHAIN_FILE="$toolchain" -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$stage" -DBUILD_SHARED_LIBS=OFF)
fetch_git https://github.com/google/glog.git "$deps/glog" "$GLOG_SOURCE_REV"
# Disable NSS username lookup in the fully static application.
cmake -S "$deps/glog" -B "$work/glog" "${common[@]}" -DWITH_GFLAGS=OFF -DBUILD_TESTING=OFF -DHAVE_PWD_H=OFF
cmake --build "$work/glog" -j"$jobs"
cmake --install "$work/glog"
glog_library="$stage/lib/libglog.a"
cmake -S "$deps/yaml-cpp" -B "$work/yaml" "${common[@]}" \
  -DYAML_CPP_BUILD_TESTS=OFF -DYAML_CPP_BUILD_TOOLS=OFF -DYAML_CPP_BUILD_CONTRIB=OFF
cmake --build "$work/yaml" -j"$jobs"
cmake --install "$work/yaml"
cmake -S "$deps/opencv-$OPENCV_VERSION" -B "$work/opencv" "${common[@]}" \
  -DBUILD_LIST=core,imgproc,imgcodecs,calib3d,features2d,flann,video,highgui \
  -DBUILD_TESTS=OFF -DBUILD_PERF_TESTS=OFF -DBUILD_EXAMPLES=OFF \
  -DBUILD_opencv_apps=OFF -DBUILD_JAVA=OFF -DBUILD_opencv_python2=OFF -DBUILD_opencv_python3=OFF \
  -DWITH_GTK=OFF -DWITH_QT=OFF -DWITH_FFMPEG=OFF -DWITH_GSTREAMER=OFF -DWITH_V4L=OFF \
  -DWITH_ITT=OFF -DWITH_QUIRC=OFF -DWITH_OPENCL=OFF -DWITH_OPENEXR=OFF -DWITH_WEBP=OFF -DWITH_TIFF=OFF -DWITH_JASPER=OFF \
  -DBUILD_ZLIB=ON -DBUILD_JPEG=ON -DBUILD_PNG=ON -DWITH_LAPACK=OFF -DWITH_PROTOBUF=OFF \
  -DCPU_BASELINE=NEON
cmake --build "$work/opencv" -j"$jobs"
cmake --install "$work/opencv"
if [[ "$micro_ros" == ON ]]; then
  "$here/micro_ros/build.sh"
fi
cmake -S "$here" -B "$core" "${common[@]}" \
  -DCMAKE_PREFIX_PATH="$stage" -DCMAKE_INSTALL_PREFIX="$out" \
  -DWORK="$work" -DDEPS="$deps" -DSCHUR_STATIC=ON -DGLOG_LIBRARY="$glog_library" \
  -DSCHUR_WITH_MICRO_ROS="$micro_ros" \
  -DSCHUR_BUILD_BENCHMARKS="${SCHUR_BUILD_BENCHMARKS:-OFF}" \
  -DSCHUR_OPT_HALFSAMPLE_NEON="${SCHUR_OPT_HALFSAMPLE_NEON:-OFF}" \
  -DSCHUR_OPT_ZMSSD_NEON="${SCHUR_OPT_ZMSSD_NEON:-OFF}" \
  -DSCHUR_OPT_FAST10_NEON="${SCHUR_OPT_FAST10_NEON:-OFF}" \
  -DSCHUR_OPT_ALIGN2D_NEON="${SCHUR_OPT_ALIGN2D_NEON:-OFF}" \
  -DSCHUR_OPT_ALIGN2D_HESSIAN_NEON="${SCHUR_OPT_ALIGN2D_HESSIAN_NEON:-OFF}" \
  -DSCHUR_OPT_WARP_AFFINE_NEON="${SCHUR_OPT_WARP_AFFINE_NEON:-OFF}" \
  -DSCHUR_OPT_WARP_TAIL2="${SCHUR_OPT_WARP_TAIL2:-OFF}" \
  -DSCHUR_SYNC_OUTLIER_OBSERVATIONS="${SCHUR_SYNC_OUTLIER_OBSERVATIONS:-OFF}"
cmake --build "$core" -j"$jobs"
cmake --install "$core"
"$here/build-camera.sh"
if [[ "$micro_ros" == ON ]]; then
  # Camera helper build is shared; copy its artifacts into the optional package.
  cp "$sdk/install/soc_sg2002_tidesense_arm64_sd/schurvins-gcc11-static/bin/"{schurvins_camera,fsin.sh} "$out/bin/"
fi
"${cross}strip" --strip-unneeded "$out/bin/schurvins_stereo" "$out/bin/schurvins_imu_test"
cp "$here/prepare-imu.sh" "$out/bin/"
cp "$here/tidesense-gyro-matrix.yaml" "$out/"
cp "$here/tests/board-imu.sh" "$out/bin/test-imu-board.sh"
"${cross}readelf" -lW "$out/bin/schurvins_stereo" > "$work/elf-program-headers.txt"
"${cross}readelf" -dW "$out/bin/schurvins_stereo" > "$work/elf-dynamic.txt"
if grep -Eq 'INTERP|NEEDED' "$work/elf-program-headers.txt" "$work/elf-dynamic.txt"; then
  echo "Static ELF verification failed" >&2; exit 1
fi
cp "$here/board-test-imu.log" "$here/IMU.md" "$here/CAMERA.md" "$here/camera-smoke.yaml" "$here/run.sh" "$here/synthetic.yaml" "$here/README.md" "$here/RECORDING.md" "$here/sources.lock" "$out/"
mkdir -p "$out/licenses" "$out/tests"
cp "$here/tests/PREDICTION-VALIDATION.md" "$out/tests/"
cp "$here/MICRO-ROS.md" "$here/CALIBRATION.md" "$here/CALIBRATION-VALIDATION.md" "$out/"
cp "$here/micro_ros/VALIDATION.md" "$out/MICRO-ROS-VALIDATION.md"
cp "$sdk/scripts/calibration/sc7u22-imu.yaml.example" "$out/"
if [[ "$micro_ros" == ON ]]; then
  mkdir -p "$out/licenses/micro-ROS"
  cp -a "$sdk/install/micro-ros/firmware/build/licenses/." "$out/licenses/micro-ROS/"
  cp "$here/micro_ros/sources.repos" "$out/micro-ros-sources.repos"
  cp "$here/micro_ros/0001-isolate-client-key-rng.patch" "$out/micro-ros-rng.patch"
  cp "$here/micro_ros/colcon.meta" "$out/micro-ros-colcon.meta"
  cp "$here/micro_ros/calibration-agent.xml" "$here/micro_ros/calibration-cyclone.xml" "$out/"
fi
cp "$here/LICENSE.PX4-SC7U22" "$out/licenses/"
# Preserve the full upstream source license notices in the downloaded trees too.
cp "$sdk/SchurVINS/LICENSE" "$out/licenses/SchurVINS-LICENSE"
cp "$deps/opencv-$OPENCV_VERSION/LICENSE" "$out/licenses/OpenCV-LICENSE"
cp "$deps/yaml-cpp/LICENSE" "$out/licenses/yaml-cpp-LICENSE"
cp "$sdk/SchurVINS/vikit/LICENSE" "$out/licenses/vikit-LICENSE"
cp "$deps/minkindr/LICENSE" "$out/licenses/minkindr-LICENSE"
cp "$deps/fast_neon/package.xml" "$out/licenses/fast-package.xml"
cp "$stage/include/glog/logging.h" "$out/licenses/glog-notices.h"
cp "$deps/glog/COPYING" "$out/licenses/glog-COPYING"
cp "$stage/include/boost/version.hpp" "$out/licenses/boost-version.hpp"
cp "$stage/include/boost/config.hpp" "$out/licenses/boost-notices.hpp"
cp "$work/sysroot/eigen/include/eigen3/Eigen/Core" "$out/licenses/Eigen-Core-notices"
cp /usr/share/doc/libboost1.74-dev/copyright "$out/licenses/Boost-copyright" 2>/dev/null || true
cp /usr/share/common-licenses/GPL-3 "$out/licenses/GPL-3" 2>/dev/null || true
(cd "$out" && sha256sum bin/schurvins_stereo bin/schurvins_camera bin/fsin.sh bin/schurvins_imu_test bin/prepare-imu.sh bin/test-imu-board.sh run.sh camera-smoke.yaml > SHA256SUMS)
{
  "${cross}g++" --version | head -1
  printf 'SCHUR_OPT_HALFSAMPLE_NEON=%s\nSCHUR_OPT_ZMSSD_NEON=%s\nSCHUR_OPT_FAST10_NEON=%s\n' \
    "${SCHUR_OPT_HALFSAMPLE_NEON:-OFF}" "${SCHUR_OPT_ZMSSD_NEON:-OFF}" "${SCHUR_OPT_FAST10_NEON:-OFF}"
  printf 'SCHUR_OPT_ALIGN2D_NEON=%s\n' "${SCHUR_OPT_ALIGN2D_NEON:-OFF}"
  printf 'SCHUR_SYNC_OUTLIER_OBSERVATIONS=%s\n' "${SCHUR_SYNC_OUTLIER_OBSERVATIONS:-OFF}"
  printf 'SCHUR_OPT_ALIGN2D_HESSIAN_NEON=%s\nSCHUR_OPT_WARP_AFFINE_NEON=%s\n' "${SCHUR_OPT_ALIGN2D_HESSIAN_NEON:-OFF}" "${SCHUR_OPT_WARP_AFFINE_NEON:-OFF}"
  printf 'SCHUR_OPT_WARP_TAIL2=%s\n' "${SCHUR_OPT_WARP_TAIL2:-OFF}"
  printf 'LIVE_CAMERA=OV9281_640x400_20fps\nLIVE_IMU=SC7U22_IIO_INT1_200Hz\nCAMERA_HELPER=GCC6_MPI_static_system_libc_dynamic\n'
  printf 'SCHUR_STATIC=%s\nSOURCE=%s\n' ON "$(git -C "$sdk/SchurVINS" rev-parse HEAD)"
  printf 'SCHUR_WITH_MICRO_ROS=%s\nMICRO_ROS_RUNTIME_DEFAULT=OFF\n' "$micro_ros"
} > "$out/BUILD-INFO"
tar -C "$(dirname "$out")" -cf "$out-a53.tar" "$(basename "$out")"
printf '\nBuilt: %s\nPackage: %s-a53.tar\n' "$out" "$out"
