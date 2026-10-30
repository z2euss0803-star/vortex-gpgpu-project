#!/usr/bin/env bash
set -euo pipefail
root=$(cd -- "$(dirname -- "$0")/../.." && pwd)
tools=/home/z2euss/vortex-tools
sdk="$root/vendor/hunet/sdk-linux-crt"
original="$root/../mosaic_original_tcu_w16a16_20261008_v1/artifacts/functional_build/kernel/libvortex.a"
out="$root/build/hunet-fused-rv32"
attempt="$out/attempt-$(date -u +%Y%m%dT%H%M%S%NZ)"
mkdir -p "$attempt/sdk"
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
common=(--sysroot="$tools/riscv32-gnu-toolchain/riscv32-unknown-elf" --gcc-toolchain="$tools/riscv32-gnu-toolchain" -Xclang -target-feature -Xclang +vortex -Xclang -target-feature -Xclang +zicond -mllvm -disable-loop-idiom-all -march=rv32imaf -mabi=ilp32f -std=c++17 -O2 -DNDEBUG -ffp-contract=off -fstack-usage -mcmodel=medany -fno-rtti -fno-exceptions -nostartfiles -nostdlib -fdata-sections -ffunction-sections "${flags[@]}" -I"$root/include" -I"$root/vendor/hunet/v5/include" -I"$sdk/kernel/include" -I"$sdk/sim/common" -I"$sdk/hw")
python3 - "$root" "$attempt" "${flags[@]}" -- "${common[@]}" <<'PY'
import hashlib,json,pathlib,sys
root=pathlib.Path(sys.argv[1]);out=pathlib.Path(sys.argv[2]);args=sys.argv[3:];cut=args.index('--')
sources={}
for directory in ('device','vendor/hunet','include/vxl','scripts/hunet_fused'):
 for p in sorted((root/directory).rglob('*')):
  if p.is_file() and '__pycache__' not in str(p):sources[p.relative_to(root).as_posix()]=hashlib.sha256(p.read_bytes()).hexdigest()
(out/'build.json').write_text(json.dumps(dict(scope='OFFLINE',defines=args[:cut],compile_flags=args[cut+1:],sources=sources),indent=2)+'\n')
PY
"$clang" "${common[@]}" -Dmain=hunet_v5_entry -MMD -MF "$attempt/v5.d" -c "$root/vendor/hunet/v5/src/device/kernel.cpp" -o "$attempt/v5.o"
"$clang" "${common[@]}" -MMD -MF "$attempt/fused.d" -c "$root/device/hunet_fused.cpp" -o "$attempt/fused.o"
"$clang" "${common[@]}" -c "$root/vendor/hunet/v5/src/device/gelu_math.c" -o "$attempt/gelu.o"
"$clang" "${common[@]}" "$attempt/fused.o" "$attempt/v5.o" "$attempt/gelu.o" -Wl,-Bstatic,--gc-sections,-T,"$sdk/kernel/scripts/link32.ld",--defsym=STARTUP_ADDR=0x80000000,-Map,"$attempt/kernel.map" "$attempt/sdk/libvortex.a" -L"$tools/libc32/lib" -lm -lc "$tools/libcrt32/lib/baremetal/libclang_rt.builtins-riscv32.a" -o "$attempt/kernel.elf"
"$tools/llvm-vortex/bin/llvm-objdump" -d "$attempt/kernel.elf" > "$attempt/kernel.disasm"
"$tools/llvm-vortex/bin/llvm-readelf" -h -l "$attempt/kernel.elf" > "$attempt/kernel.headers"
sha256sum "$clang" "$gcc" "$original" "$attempt/sdk/libvortex.a" "$tools/libc32/lib/libm.a" "$tools/libc32/lib/libc.a" "$tools/libcrt32/lib/baremetal/libclang_rt.builtins-riscv32.a" > "$attempt/dependencies.sha256"
python3 "$root/scripts/hunet_fused/audit_rv32.py" "$attempt" > "$attempt/audit.log"
printf '%s\n' "$attempt" > "$out/LATEST"
echo FUSED_RV32_BUILD_AUDIT_PASS
echo "$attempt"
