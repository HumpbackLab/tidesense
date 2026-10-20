#!/usr/bin/env bash
set -eo pipefail
sdk=$(cd "$(dirname "$0")/../.." && pwd)
cd "$sdk"
# shellcheck source=/dev/null
source build/envsetup_soc.sh >/dev/null
defconfig sg2002_tidesense_arm64_sd >/dev/null
make -C cvi_mpi component
make -C cvi_mpi/sample/common
out="$sdk/install/soc_sg2002_tidesense_arm64_sd/tidesense-uvc"
mkdir -p "$out/bin"
"${CROSS_COMPILE}gcc" -std=gnu11 -O2 -Wall -Wextra -Werror \
 -I cvi_mpi/include -I cvi_mpi/modules/isp/include/cv181x \
 -I cvi_mpi/sample/common -I cvi_mpi/component/isp/common -I cvi_mpi/3rdparty/inih \
 -I linux_5.10/build/sg2002_tidesense_arm64_sd/usr/include \
 scripts/uvc/main.c scripts/uvc/camera.c scripts/uvc/imu.c scripts/uvc/protocol.c scripts/uvc/output.c \
 -o "$out/bin/tidesense_uvc" \
 -Wl,--wrap=CVI_VI_SetChnAttr -Wl,--wrap=CVI_ISP_SetPubAttr \
 -Wl,-Bstatic -Wl,--start-group -L cvi_mpi/lib -L cvi_mpi/lib/3rd \
 -lsample -lsys -lvi -lvo -lvpss -lrgn -lgdc -lvenc -lcvi_bin -lcvi_bin_isp \
 -lisp -lisp_algo -lae -laf -lawb -lsns_full -lini \
 -Wl,--end-group -Wl,-Bdynamic -lpthread -lm -ldl -latomic -lrt
"${CROSS_COMPILE}strip" "$out/bin/tidesense_uvc"
cp scripts/uvc/run.sh scripts/uvc/sensor_cfg.ini scripts/uvc/README.md \
 scripts/uvc/VALIDATION.md scripts/uvc/decode.py "$out/"
chmod +x "$out/run.sh"
tar -C "$(dirname "$out")" -cf "$out-a53.tar" tidesense-uvc
echo "Built $out-a53.tar"
