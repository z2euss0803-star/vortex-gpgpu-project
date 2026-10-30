#!/bin/bash
set -euo pipefail
: "${CROSS_COMPILE:?existing AArch64 toolchain required}"
: "${SOURCE_COMMIT:?sealed source commit required}"
test "$(cat SOURCE_COMMIT)" = "$SOURCE_COMMIT"
run_id=${RUN_ID:-cross-$(date -u +%Y%m%dT%H%M%SZ)}
mkdir "evidence/$run_id"
{
 id
 hostname
 uname -a
 "${CROSS_COMPILE}gcc" --version
 make userspace-cross CROSS_COMPILE="$CROSS_COMPILE" SOURCE_COMMIT="$SOURCE_COMMIT"
 for file in build/aarch64/libvortex.so build/aarch64/libvortex-zynq.so build/aarch64/vxl_info build/aarch64/vxl_copy_test build/aarch64/vxl_vecadd build/aarch64/vxl_cnn build/aarch64/test_cnn_identity; do
  "${CROSS_COMPILE}readelf" -h "$file" | grep 'Machine:.*AArch64'
  if [[ "$file" != *.so ]]; then
   "${CROSS_COMPILE}readelf" -l "$file" | grep 'Requesting program interpreter: /lib/ld-linux-aarch64.so.1'
  fi
  "${CROSS_COMPILE}readelf" -d "$file" | grep NEEDED
  sha256sum "$file"
 done
 echo AARCH64_BUILD_PASS_NOT_BOARD
} >"evidence/$run_id/userspace.log" 2>&1
cat "evidence/$run_id/userspace.log"
