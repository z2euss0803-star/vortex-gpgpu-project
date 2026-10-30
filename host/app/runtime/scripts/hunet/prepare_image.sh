#!/usr/bin/env bash
# Offline image production only. No block device, board SSH or PL download.
set -euo pipefail
base=/home/z2euss/workspace/petalinux/codex_linux_backend_cnn_i32_reviewfix_20261011/env_backend
candidate= release= xsa= cache_project=
while (($#)); do
    case "$1" in
        --base) base=$2;;
        --candidate) candidate=$2;;
        --release) release=$2;;
        --xsa) xsa=$2;;
        --cache-project) cache_project=$2;;
        *) echo "Unknown argument: $1" >&2; exit 2;;
    esac
    shift 2
done
test -n "$candidate" && test -n "$release" && test -n "$xsa"
script_dir=$(cd -- "$(dirname -- "$0")" && pwd)
args=(--base "$base" --candidate "$candidate" --release "$release" --xsa "$xsa")
if test -n "$cache_project"; then args+=(--cache-project "$cache_project"); fi
python3 "$script_dir/prepare_image.py" "${args[@]}"
project=$candidate/env_backend
# PetaLinux2020.1's setup references unset variables; do not alter its source.
set +u
source /home/z2euss/petalinux/2020.1/settings.sh
set -u
date -u +%FT%TZ > "$candidate/config.start"
set +e
petalinux-config --get-hw-description "$(dirname -- "$xsa")" -p "$project" --silentconfig > "$candidate/config.log" 2>&1
rc=$?
set -e
printf '%s\n' "$rc" > "$candidate/config.exit"
test "$rc" -eq 0
date -u +%FT%TZ > "$candidate/build.start"
set +e
petalinux-build -p "$project" > "$candidate/build.log" 2>&1
rc=$?
set -e
printf '%s\n' "$rc" > "$candidate/build.exit"
date -u +%FT%TZ > "$candidate/build.end"
test "$rc" -eq 0
images=$project/images/linux
test ! -e "$images/BOOT.BIN"
date -u +%FT%TZ > "$candidate/package.start"
if test -f "$candidate/retained-BOOT.BIN"; then
    cp -p -- "$candidate/retained-BOOT.BIN" "$images/BOOT.BIN"
    printf '%s\n' 'REUSED_FROZEN_HW4_BOOT_BIN_NOT_REBUILT' > "$candidate/package.log"
    printf '0\n' > "$candidate/package.exit"
    date -u +%FT%TZ > "$candidate/package.end"
    printf '%s\n' 'HW4_HUNET_HYBRID_IMAGE_BUILT_NOT_BOARD'
    exit 0
fi
set +e
petalinux-package --boot -p "$project" --format BIN --fsbl "$images/zynqmp_fsbl.elf" --fpga "$release/hardware/top.bit" --pmufw "$images/pmufw.elf" --atf "$images/bl31.elf" --u-boot "$images/u-boot.elf" --output "$images/BOOT.BIN" > "$candidate/package.log" 2>&1
rc=$?
set -e
printf '%s\n' "$rc" > "$candidate/package.exit"
date -u +%FT%TZ > "$candidate/package.end"
test "$rc" -eq 0
printf '%s\n' 'HW4_HUNET_IMAGE_BUILT_NOT_BOARD'
