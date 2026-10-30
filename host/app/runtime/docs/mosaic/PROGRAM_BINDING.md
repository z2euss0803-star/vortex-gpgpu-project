# Mosaic program metadata admission (T10 component)

This component is not the engine/driver lifecycle, an executable release, or GPU evidence. The public request remains 176 bytes; the independent Mosaic job remains 128 bytes. Profile 6 / program 12 do not replace any old profile or ID.

MosaicBindingV1 is 512 bytes: eight LE u32 header fields (magic 0x31424d56, version 1, bytes 512, profile 6, program 12, ABI major 1, features 1, reserved zero), fourteen SHA256 digests, and 32 reserved zero bytes. Digests, in order: software source, flat program, ELF, model, assets, graph catalog, layout catalog, RV32 compiler, runtime, runner, kernel config, hardware manifest, bit, XSA. No digest may be zero. The host has an independently trusted complete binding SHA; a caller's CRC or digest is not authority.

Registration verifies actual flat bytes, model/package identity, fixed 16-layer geometry, canonical layout/graph/runtime roots, CRT/TLS/stack bounds and mutable ranges. It publishes the program only after every check succeeds. The test fixture uses a clearly labeled fake flat payload and fake tool/hardware digests; it cannot be shipped or admitted as a real GPU program.

Canonical catalog serialization is implemented independently in `tests/mosaic_s8/program_fixture.py` and `core/mosaic_program.c`. It uses explicit LE words, not JSON or compiler struct padding. Layout records bind all nine address/size/guard/readonly roles; runtime binds mutable ranges, CRT, TLS, stack and runtime dependencies. A final generated closed catalog is still required in T19.

The frozen SDK's `vx_start.S` does not initialize FCSR. The future Mosaic device entry must set FCSR to zero and verify RNE on every participating hart before FP32 work. Host FE_TONEAREST checks and RV32 object compilation do not satisfy this entry requirement. Do not silently alter the frozen SDK.

Software work ranges will be limited to 4096 items per submission, with phase progress committed only after trusted completion/evidence/reap. Declaring the limit here does not prove a hardware watchdog duration; real timing remains unmeasured. No per-tile Linux submission is planned.

2026-10-19 verification: Ubuntu ordinary + ASan/UBSan joint assets/contract/tile/math/16-layer graph/program tests exit 0 (log SHA fc50c3033865b15111e4927b9fb1356643e90973da5978caa7a2534d7594cfea). Eight matching RV32 objects and an AArch64 metadata test ELF compile; the latter SHA is 0afa06f271abd86502a6aedeb65bd0d56455c6572eb5b144a40e89eb6676ba2d, GLIBC 2.17/rootfs dependencies checked. The ELF was not run on AArch64. New real GPU raw cycles are null.
