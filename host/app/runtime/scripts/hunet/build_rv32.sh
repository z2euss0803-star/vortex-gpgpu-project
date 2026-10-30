#!/usr/bin/env bash
set -euo pipefail
root=$(cd -- "$(dirname -- "$0")/../.." && pwd)
out="$root/build/hunet-rv32"
while (($#)); do
 case "$1" in --out) out="$root/$2";shift 2;; --release) release="$2";shift 2;; *) echo "unknown option: $1" >&2;exit 2;; esac
done
tools=/home/z2euss/vortex-tools
sdk="$root/vendor/hunet/sdk-linux-crt"
original="$root/../mosaic_original_tcu_w16a16_20261008_v1/artifacts/functional_build/kernel/libvortex.a"
attempt="$out/attempt-$(date -u +%Y%m%dT%H%M%S%NZ)"
mkdir -p "$attempt/sdk" "$attempt/wmma"
unset CONFIGS DEBUG PERF CXXFLAGS CFLAGS CPPFLAGS LDFLAGS MAKEFLAGS MFLAGS GNUMAKEFLAGS XLEN EXT_V_ENABLE LD_PRELOAD VORTEX_DRIVER
flags=(-DNUM_CLUSTERS=1 -DNUM_CORES=4 -DSOCKET_SIZE=4 -DNUM_WARPS=4 -DNUM_THREADS=4 -DNUM_BARRIERS=2 -DGBAR_ENABLE -DICACHE_SIZE=16384 -DDCACHE_SIZE=16384 -DDCACHE_WRITEBACK=0 -DPLATFORM_MEMORY_NUM_BANKS=1 -DPLATFORM_MEMORY_DATA_SIZE=64 -DMEM_BLOCK_SIZE=64 -DMEM_ADDR_WIDTH=32 -DLMEM_BASE_ADDR=1879048192 -DFPU_DSP -DEXT_TCU_ENABLE -DTCU_DSP -DEXT_TEX_ENABLE -DHUNET_SCALAR_CHUNK=32 -DIO_COUT_ADDR=0x10002040 -DIO_MPM_ADDR=0x10002080 -DXLEN_32)
gcc="$tools/riscv32-gnu-toolchain/bin/riscv32-unknown-elf-gcc"
ar="$tools/riscv32-gnu-toolchain/bin/riscv32-unknown-elf-gcc-ar"
clang="$tools/llvm-vortex/bin/clang++"
cp "$original" "$attempt/sdk/libvortex.a"
for name in vx_start.S vx_print.S vx_perf.c; do
 "$gcc" -march=rv32imaf -mabi=ilp32f -O3 -mcmodel=medany -fno-exceptions -fdata-sections -ffunction-sections "${flags[@]}" -I"$sdk/kernel/include" -I"$sdk/hw" -c "$sdk/kernel/src/$name" -o "$attempt/sdk/$name.o"
done
"$ar" rcs "$attempt/sdk/libvortex.a" "$attempt/sdk/vx_start.S.o" "$attempt/sdk/vx_print.S.o" "$attempt/sdk/vx_perf.c.o"
common=(--sysroot="$tools/riscv32-gnu-toolchain/riscv32-unknown-elf" --gcc-toolchain="$tools/riscv32-gnu-toolchain" -Xclang -target-feature -Xclang +vortex -Xclang -target-feature -Xclang +zicond -mllvm -disable-loop-idiom-all -march=rv32imaf -mabi=ilp32f -std=c++17 -O2 -DNDEBUG -ffp-contract=off -mcmodel=medany -fno-rtti -fno-exceptions -nostartfiles -nostdlib -fdata-sections -ffunction-sections "${flags[@]}" -I"$root/vendor/hunet/v5/include" -I"$sdk/kernel/include" -I"$sdk/sim/common" -I"$sdk/hw")
libraries=("$attempt/sdk/libvortex.a" -L"$tools/libc32/lib" -lm -lc "$tools/libcrt32/lib/baremetal/libclang_rt.builtins-riscv32.a")
"$clang" "${common[@]}" "$root/vendor/hunet/v5/src/device/kernel.cpp" "$root/vendor/hunet/v5/src/device/gelu_math.c" -Wl,-Bstatic,--gc-sections,-T,"$sdk/kernel/scripts/link32.ld",--defsym=STARTUP_ADDR=0x80000000,-Map,"$attempt/kernel.map" "${libraries[@]}" -o "$attempt/kernel.elf"
"$clang" "${common[@]}" "$root/device/hunet_wmma.cpp" -Wl,-Bstatic,--gc-sections,-T,"$sdk/kernel/scripts/link32.ld",--defsym=STARTUP_ADDR=0x80000000,-Map,"$attempt/wmma/kernel.map" "${libraries[@]}" -o "$attempt/wmma/kernel.elf"
for d in "$attempt" "$attempt/wmma"; do
 "$tools/llvm-vortex/bin/llvm-objdump" -d "$d/kernel.elf" > "$d/kernel.disasm"
 "$tools/llvm-vortex/bin/llvm-readelf" -h -l "$d/kernel.elf" > "$d/kernel.headers"
 id=6;[[ "$d" == */wmma ]] && id=7
 python3 "$root/scripts/hunet/audit_program.py" --elf "$d/kernel.elf" --out "$d" --program-id "$id" > "$d/audit.log"
done
sha256sum "$clang" "$gcc" "$original" "$attempt/sdk/libvortex.a" "$tools/libc32/lib/libm.a" "$tools/libc32/lib/libc.a" "$tools/libcrt32/lib/baremetal/libclang_rt.builtins-riscv32.a" > "$attempt/dependencies.sha256"
find "$root/vendor/hunet" -type f -print0 | sort -z | xargs -0 sha256sum > "$attempt/source-dependencies.sha256"
python3 "$root/scripts/hunet/freeze_programs.py" --attempt "$attempt" --out "$out"
echo RV32_LINUX_CRT_AND_WMMA_BUILD_STATIC_AUDIT_PASS
