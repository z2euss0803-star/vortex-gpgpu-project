# Matching offline cross-builds

`guest_gate` rejects unknown coordination, another heavy task, low memory/disk,
wrong SDK or kernel, and missing measurements. Builds run in private native
directories; frozen W4 hardware and the other user's C projects remain unchanged.

An A clean Git archive and per-file Git-blob/SHA inventory are checked on Ubuntu.
Compilation receipts bind the exact inventory, catalog, compiler inputs, actual
ELFs, runtime libraries and natural exits. T22/T23 must compile and seal their
final source commit; a preliminary T21 receipt cannot relabel a later commit.

RV32 uses the frozen 4-core/4-warp/4-thread rv32imaf/ilp32f configuration,
unmodified WMMA shape and private PS CRT addresses. Exact `vx_spawn.c` and both
assembly/C printing implementations replace stale archive members. C1_T/H copy
the frozen program9 wire byte for byte. Program10 candidates contain actual TCU,
barrier and fence instructions, checked from their ELF machine words.

The static stack envelope sums every compiled frame and every fixed linked
library frame. Its condition is the frozen nonrecursive numerical/callback paths
and fixed diagnostic formats; the separate CRT audit checks the stack pivot and
8192-byte stride. This is a conservative static admission, not a dynamic high
water measurement. Unknown dynamic external frames are refused. LLVM frame
layout remarks distinguish compiler `Spill` and `Variable` slots; saved register
slots are included, so these counts are not dynamic register-pressure traffic.

AArch64 always passes `OPT_CRYPTO=1`, retaining the compile-time refusal of the
host-only SHA2 stub. Only digest_sha2_block.o is built with +crypto; actual
disassembly checks baseline/crypto isolation. Rootfs runtime compatibility uses
the matching rootfs.tar.gz inventory. User HWCAP and kernel shash selection still
need later runtime evidence. Kernel builds retain W=1/Werror, exact .config and
Module.symvers identities; the standalone kernel target is not a built-in image
or board validation.

Old tmp wrappers may refer to archived paths. Bind retained compiler/sysroot and
kernel build paths explicitly. LLVM uses a private host loader/runtime and an
explicit resource directory. No system package or library upgrade is required.
