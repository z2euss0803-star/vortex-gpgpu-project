# SDD ledger — plan: docs/runtime/PLAN_ENTRY.md

Plan approved by user: 批准计划，沿用本会话从T01执行. Approved plan commit 4f34c46c9f602f6db78bab173d2f0dbef9a278d2; original written spec 72159ec.

## Pre-flight shared interfaces

| Producer → consumer | Shared interface | Finding |
|---|---|---|
| T01 → all | argv runner, frozen manifest, natural exit | Offline GPU cycles remain null; natural exit distinct from semantics. |
| T02 → T03/T05 | legacy clients, network and DP | Import explicit fix commit; preserve old UAPI and programs. |
| T05 → T06/T07/T09/T10 | common C++ types and backend | One definition; legacy adapter cannot advertise generic GPU support. |
| T06/T07 → T09/T17/T19 | package, prepared graph, tensor leases | Packages and kernel identity independent; no arbitrary graph logic in kernel. |
| T08/T09 → T17/T18 | CPU pool, cancellation, immutable results | Retain references through active cancel and DP retirement. |
| T10/T11 → T12–T16 | C UAPI, buffer/program references | Preserve old major1; reserve major2 only after collision scan. |
| T15/T16 → T17/T19 | accepted job and completion lifetime | Copy faults must not duplicate execution or lose completion. |
| T04/T21 → T22 | resources and exact build inputs | 15 GiB image gate remains; C work protected. |
| T20/T22 → T23 | candidate identity and deployment | No inherited board permission; no offline raw cycle claims. |

Ruling: execute the T-prefixed, split Chinese plan with this durable ledger and portable Python test commands instead of Bash-only task-start/task-done parsing — existing approved task IDs and Windows host require equivalent evidence bookkeeping — cost if wrong: manual task-state mismatch, mitigated by task commit ranges and stored natural exits.

## T01 in progress — BASE 4f34c46

- Functional RED T01-red-002: 7 tests, 6 assertion failures (natural exit, overwrite refusal, drift, empty/escaping manifest, receipt creation); A orchestration log retained separately.
- Prior setup path error T01-red-001 is not functional RED. Fixed local build temporary directory, no source or device behavior involved.
- GPU raw cycles: null. No board, SD or PL access.

Task 01: complete — BASE 4f34c46; A and B Ubuntu evidence unit suite 7/7 each, natural exits 0; 4928 source members verified; 97 original dirty files and 74 release members unchanged; latest A RY/13151, B DESKTOP-O7FQKAL/think, Ubuntu z2euss-virtual-machine/z2euss verified. New UTF-8 Chinese round trip verified exactly. Correct Oct20 kernel_build and four input SHA recorded in env.json.
Evidence: work/linux_nn_runtime_upgrade_20261021/T01-A-green-001, T01-remote-001 (failed preparation retained), T01-remote-002, T01-returned. Offline gpu_raw_cycles=null.
Ruling: the older B fieldfix directory is a minimal validation source, not a complete frozen checkout — restore runtime source from full immutable export and verify all4928 members — cost: about 0.42 GiB temporary disk, retained with failed partial source rather than discarded.

## T02 — BASE 642f056

Explicit import: 12894074d517b90e40782d270bc603909d718e2e → 7455feb. Reviewed cold-idle source change, ioctl lock wrapper, readonly refusal snapshots, network recipe/audit, ARGB8888 and <=60Hz selection/restore tests. Original red-003 exit2 and green-001 ordinary/SAN exit0 inputs retained. No preview ko deployed.

Task 02: complete — T02-regression-001: Python network/image/legacy suite natural0; actual driver profile-driver/profile-switch/hunet-release ordinary0 and ASan/UBSan0; DP native-restore/kms/present ordinary0 and ASan/UBSan0. Five runner receipts all semantic PASS, no sanitizer error. 184 golden old files unchanged; ioctl1..22/64..69 unchanged, proposed0x80..0x8b unoccupied. Source admission before and after every run passed. GPU raw cycles null; not board evidence.

Harness records: initial local Git fetch failed with Windows MSYS named-object sandbox denial, no import applied; same local fetch succeeded under reviewed escalation, then cherry-pick succeeded. An A-side legacy test command initially used vault cwd and failed import (not functional RED); correct candidate cwd and full B suite passed. Original tool output retained in conversation, T02-remote-001 retains complete remote output. Actual field EBUSY predicate and Mosaic watchdog remain OPEN.

## T03 — BASE 1966646

Task 03: complete — valid functional REDs: batch limit coverage T03-red-001 exit2; prefix stopping T03-prefix-red-002 exit2; full small-batch graph admission T03-budget-red-001 exit2. Each corrected by independent run. T03-green-001 ordinary/SAN passes real client/default77 path and complete12x8 16-layer actual math for batches4/16/64/256 against unchanged frozen reference; T03-final-001 ordinary/SAN passes added pre-BEGIN job/evidence admission, bounded prefix ACK/REAP/owner lifetime, legacy default and diagnostic entry compilation. Python evidence/legacy/workload suites pass; no GPU/device code or old UAPI/generated digest change. GPU raw cycles null.
Full1280x960 static jobs/evidence: {'4096': (1597, 817664), '4': (922291, 472212992), '16': (231091, 118318592), '64': (58291, 29844992), '256': (15091, 7726592)}. Static workload is not measured latency or a root-cause conclusion. Diagnostic max8 jobs /4096 raw evidence bytes; EINPROGRESS means DIAGNOSTIC_PREFIX_STOPPED_NOT_FRAME_COMPLETE. No readback, END, owner release, application switch or retry after held prefix.
Ruling: expose the diagnostic as a source-bound borrowed-client entry in tools/vxl_mosaic_diag.c; final provider/CLI binding belongs to T17/T21 — no unsafe standalone unbound board executable — cost if wrong: additional integration work before field use.
Ruling: add user-space-only frame_max_jobs/frame_max_evidence_bytes and setter to enforce full small-batch pre-BEGIN admission — approved budget requirement needs an executable gate — cost if wrong: source-level client callers must recompile; old ioctl176B, GPU job128B, program12 and old default4096 remain unchanged.
Retained harness errors: prefix fixture initial main macro collision (T03-prefix-red-001, not functional RED), PowerShell attempted OUT variable expansion during a non-writing local helper command (tool transcript); fixed with existing session shim and saved Python Makefile editor. No error evidence overwritten. Original T02/T03 logs exported/verified in work/linux_nn_runtime_upgrade_20261021/T02-T03-verified; 12x8 host math is not formal full-resolution quality or board PASS.

## T04 — BASE ae2653c — guards verified, capacity remains OPEN

- Functional RED on A: 10 safety/retention assertions fail, actual Linux lock test skipped on Windows (T04-A-red-001).
- Ubuntu T04-guards-inventory-001: all14 archive/release tests PASS, including Linux active lock, truncated/missing/duplicate/drifted archive, archive/source link escape, added/changed source, shared dependency and dry-run/exact temporary-file release. No actual engineering cache released.
- Fresh Ubuntu free4675915776 bytes, below16106127360-byte image gate. Old HUNet-v5 build/tmp only241459200 bytes; env-smoke tmp68112384; oldCNN tmp24576. Most apparent footprint is downloads/sstate, explicitly protected/shared-or-unknown. No exclusive cache yielding the needed11.43 GB established by this bounded inventory. Retain them rather than infer ownership from directory names.
- A F free1936105472 bytes; A D free64137297920 bytes. Any large future archive belongs on separately verified A D storage, not a new F copy.
- T04 step4 capacity closure DEFERRED/RESOURCE_BLOCKED. No image build attempted. Continue T05 under its own fresh unit/SAN gate as explicitly allowed by the plan; T04 is not marked complete.
- Evidence: work/linux_nn_runtime_upgrade_20261021/T04-A-red-001, T04-remote-001, T04-inventory.json. GPU raw cycles null. C/source/kernel/SDK/models/images/failures/testdisk unchanged.

## T05 — BASE 7c656da

Task 05: complete — functional RED T05-red-001 rejects missing old result; timing RED offline/nested/cross-host each natural-6; T05-late-error-red-001 natural2 proves raw result lost on end-log failure. T05-green-004 wrapper ordinary/SAN both0 and four actual C++ event tests via Python0; evidence/184 old identities0. T05-legacy-regression-002 actual old HUNet runner and Mosaic session ordinary/SAN both0, frozen historical fixture inputs verified. No device opened: wrapper unit records zero open/close, borrowed fd77; actual adapters use their original wrapped hardware fixtures.
Common Result<T>/Result<void> constructors enforce value/error exclusivity; event sink keeps uint64 integers and offline null cycles; parent intervals subtract child union and cross-host/clock totals are rejected. Legacy wrapper validates borrowed owner, frozen request/FP32 input/shape/digest/diagnostic scope, preserves exact original result JSON and known numeric class, returns original errors and blocks successors. End-log failure carries original result in error detail and never publishes success. Legacy remains a fixed-graph adapter, not IGpuBackend capability or new-model extension PASS.
Ruling: host GCC7 uses std::experimental::filesystem::path compatibility while target GCC9 uses required std::filesystem::path — frozen host toolchain lacks standard filesystem — cost: host-only C++ type differs, all interchange remains explicit JSON/C wire, target type unchanged.
Ruling: IGpuBackend declares all copy directions, buffer/program release and ProgramRef generation, with no implementation or available capability claims — downstream lifecycle cannot safely use a handle-only return — cost: later concrete backends implement these explicit methods.
Preserved failures: T05-green-001 GCC7 optional<Error> maybe-uninitialized under Werror, fixed with tagged direct Result construction (no warning suppression); T05-legacy-regression-001 missing original hardcoded host fixture paths, fixed by verified two-member extraction into private input paths, not modifying old test/source; automatic approval timeout launched no command, authorized same-scope retry succeeded. Raw logs and receipts in work/linux_nn_runtime_upgrade_20261021/T04-T05-verified. Current free4457852928B; image gate remains blocked. GPU raw cycles null; no board/SD/PL.
