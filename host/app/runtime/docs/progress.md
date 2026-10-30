# Execution ledger — plan: F:/obsidian-codex/codex/60-输出/Vortex-Linux后端闭环实施与6.1Sol交接-2026-10-08.md

FINAL 2026-10-09: OFFLINE_READY for H0 fixed diagnostic subset. Tasks0–9 offline gates pass; B eight host gates, 9 contract checks, 48 guarded copy cases, 100 jobs/10 cycles/fault injection, ASan/UBSan, real public stub and dispatch fixture, AArch64 GCC9.2 libraries/tools and actual Xilinx5.4 module compile complete. Final target audit/package recovered to A: SHA bcd2a210014409516eeeb8e5cc1963b2c96ed9ac0e1fa7b6a7e0cb986f53a4ce; 8/8 artifact hashes, host/target ABI equal, 26 protected inputs unchanged. state.json and docs/离线交付报告-20261009.md are the current status; dated paragraphs below retain investigation history, not final conclusions. H0/H1 remain NOT_RUN. Task10, actual D2 PS reservation/boot overlap/clock-reset integration and full original simx/xrt/opae regression remain open. SD/board untouched; one SD and reader available.

2026-10-09: user explicitly authorizes tasks 0–9 offline implementation/tests/cross build. Board powered off; no board probes, SD writes, bit programming.

Task 0: RUNNING. A=RY/ry\13151. Original Vortex repository HEAD=d425a56ef6299971cce5de629d6fe626b98cafa9, runtime status clean. Source root is a new isolated repository on codex/linux-backend, not the vault Git root.

2026-10-09 continuation: Task0 source isolation/SSH UTF8 round trip/native ext4/toolchain lock PASS. 26 original inputs preserved by SHA; roundtrip SHA c388a4ab0dd40b359b0911a795a08e1dc694e2731c42a616d7eb6c72e146ea8e. Task1 eight tests PASS (RED retained; port SIGIS corrected to use actual wiring SIGNAME). H1 remains PROPOSED.

Tasks2–4 shared ABI/range/pool/jobs PASS in B Ubuntu and A WSL. Task6 48 guard copy cases + shared CDMA old-IOC/IDLE/error/timeout/high-bit cases PASS on host. Task7 full protocol model + production exact-program validator PASS on host. Task8 genuine public stub dynamic load, BSS, synthetic vecadd chain and 10 close cycles PASS on A WSL. 100 sequential jobs and failure/close quarantine PASS; ASan/UBSan checks run separately.

Target early revision built libvortex.so, backend, tests and vxl_linux.ko against actual GCC9.2/Xilinx5.4. Audit found invalid dynamic-loader interpreter /lib/ld- in Yocto compiler defaults: recipe now sets the verified platform loader explicitly; latest revision still needs target rebuild/audit. B SSH subsequently timed out on A→B banner; no board probe was attempted. Continue A tasks while B reconnect is pending; no OFFLINE_READY claim yet.

Correction from direct ELF evidence: `file` on Ubuntu18 displays /lib/ld- even for correctly linked binaries. loader_probe.sh compiled without the explicit linker option, and `readelf -p .interp` confirmed /lib/ld-linux-aarch64.so.1 for both default and final binaries. The earlier interpretation of that display as a broken compiler default was unsupported and is withdrawn. Explicit loader option is retained as a reproducible contract; ELF .interp is the acceptance source.

Fresh review found delivered filename vecadd16 does not imply16 total elements: H0 has4×4×4 threads, source host uses VEC_COUNT64. Real adapter/tool now validate256 bytes per array; independent negative test rejects a last256-byte window shifted by4. PL mem_info counts only PL objects, avoiding PS code reservation underflow in PL free capacity. Failure evidence retained before both fixes.

Ruling on decomposition: compact core/engine.c and driver/vxl_hw.c own object/transfer/submit adapters; these replace multiple proposed filenames while retaining shared logic and tests. Proposals named files, not independent duplicate implementations. Diagnostic tools use .cpp where they call C++ public runtime.

Pre-flight interfaces: tasks 1/2 produce profiles/UAPI consumed by 3–8; pure core logic must build as both host C and kernel C. Tasks 3/4 own memory and lifecycle; hardware adapters cannot bypass them. Task 8 must run the genuine public stub, not an imitation API.

Ruling: user requests native execution without subagents; final review will be an explicit fresh reread and adversarial tests by this executor, without dispatching another agent. Cost: no independent reviewer.

Ruling: preserve progress/evidence under this source repository rather than deleting skill scratch after completion. User evidence retention rules override scratch cleanup.

Ruling: H1 contract acceptance and real PS DMA placement remain external gates; implement offline logic and fail-closed hardware checks while reporting these gates separately, never fabricate acceptance.

