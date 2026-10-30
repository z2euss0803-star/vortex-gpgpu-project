# CNN offline release audit

This stage adds release integrity checks, not permission to execute a board job.
The source snapshot binds source commit, exact spec/plan SHA, source file inventory,
profile bit/XSA, all four 4096-byte programs and all five cases' nine guarded
tensors plus golden outputs. The verifier regenerates fixed fixtures, so changing
a tensor and updating its SHA does not hide a numerical/guard change.

scripts/collect_evidence.py runs 17 named offline gates separately. Each record
has the command, UTC start/end, host-only duration, exit and stdout/stderr SHA.
Run directories cannot be reused. H0/H1 remain NOT_RUN. A dirty source receipt
cannot seal a release; collect after committing the tested source.

Host tests include independent CPU arithmetic and a serial RV32 model. The
96-job session backend supplies test results and is TEST_PROTOCOL_NOT_GPU.
It does not prove actual GPU execution, SIMT, FPGA timing or board performance.
SIMT_RTL/AARCH64/IMAGE/SD_DEPLOY/H0_CNN/H1 stay NOT_RUN in this source-stage
manifest. Later stage receipts must explicitly bind this release, rather than
editing this source-stage manifest to promote its evidence.

Integrity checking detects mismatches and omissions. A JSON PASS is not a
cryptographic signature, and unit-test receipt fixtures are never physical
execution evidence. Source snapshot and per-file hashes are retained for later
transfer and independent auditing.

SD boot remains the selected route. JTAG is optional debugging, not a required
new boot path. The driver does not support runtime PL reload/recovery; breakpoint
runs are separate from performance samples. Task9 must still integrate vxl_cnn
into the AArch64 and derived image targets and tighten the real CLI's structured
candidate identity check. No board execution is authorized by this audit.
