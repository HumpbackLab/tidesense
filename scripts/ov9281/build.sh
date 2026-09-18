#!/usr/bin/env bash
set -eo pipefail
sdk=$(cd "$(dirname "$0")/../.." && pwd)
cd "$sdk"
source build/envsetup_soc.sh >/dev/null
defconfig sg2002_tidesense_arm64_sd >/dev/null
make -C cvi_mpi component
make -C cvi_mpi/sample/common
mkdir -p install/ov9281-test
${CROSS_COMPILE}gcc -O2 -Wall -Wextra -Werror -I cvi_mpi/include \
 -I cvi_mpi/modules/isp/include/cv181x -I cvi_mpi/sample/common \
 -I cvi_mpi/component/isp/sensor/cv182x/ov_ov9281 \
 -I cvi_mpi/component/isp/common -I cvi_mpi/3rdparty/inih \
 scripts/ov9281/capture.c -o install/ov9281-test/capture -Wl,--wrap=CVI_VI_SetChnAttr \
 -Wl,-Bstatic -Wl,--start-group -L cvi_mpi/lib -L cvi_mpi/lib/3rd \
 -lsample -lsys -lvi -lvo -lvpss -lrgn -lgdc -lvenc -lcvi_bin -lcvi_bin_isp \
 -lisp -lisp_algo -lae -laf -lawb -lsns_full -lini \
 -Wl,--end-group -Wl,-Bdynamic -lpthread -lm -ldl -latomic -lrt
${CROSS_COMPILE}strip install/ov9281-test/capture
${CROSS_COMPILE}gcc -O2 -Wall -Wextra -Werror -ffunction-sections -fdata-sections \
 -I cvi_mpi/include -I cvi_mpi/modules/isp/include/cv181x \
 -I cvi_mpi/component/isp/sensor/cv182x/ov_ov9281 \
 scripts/ov9281/mode_check.c -o install/ov9281-test/mode_check \
 -Wl,--gc-sections -Wl,-Bstatic -L cvi_mpi/lib -lsys -Wl,-Bdynamic -lpthread -lm
${CROSS_COMPILE}strip install/ov9281-test/mode_check

cp scripts/ov9281/fsin.sh install/ov9281-test/fsin.sh
