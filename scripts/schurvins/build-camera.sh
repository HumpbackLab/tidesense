#!/usr/bin/env bash
set -eo pipefail
sdk=$(cd "$(dirname "$0")/../.." && pwd)
cd "$sdk"
source build/envsetup_soc.sh >/dev/null
defconfig sg2002_tidesense_arm64_sd >/dev/null
make -C cvi_mpi component
make -C cvi_mpi/sample/common
out="$sdk/install/soc_sg2002_tidesense_arm64_sd/schurvins-gcc11-static/bin"
mkdir -p "$out"
${CROSS_COMPILE}gcc -O2 -Wall -Wextra -Werror -I cvi_mpi/include \
 -I cvi_mpi/modules/isp/include/cv181x -I cvi_mpi/sample/common \
 -I cvi_mpi/component/isp/common -I cvi_mpi/3rdparty/inih \
 scripts/schurvins/camera_source.c -o "$out/schurvins_camera" -Wl,--wrap=CVI_VI_SetChnAttr \
 -Wl,-Bstatic -Wl,--start-group -L cvi_mpi/lib -L cvi_mpi/lib/3rd \
 -lsample -lsys -lvi -lvo -lvpss -lrgn -lgdc -lvenc -lcvi_bin -lcvi_bin_isp \
 -lisp -lisp_algo -lae -laf -lawb -lsns_full -lini \
 -Wl,--end-group -Wl,-Bdynamic -lpthread -lm -ldl -latomic -lrt
${CROSS_COMPILE}strip "$out/schurvins_camera"
cp scripts/ov9281/fsin.sh "$out/fsin.sh"
