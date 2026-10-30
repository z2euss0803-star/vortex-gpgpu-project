# Mosaic/HUNet DP candidate error ledger

Every run is distinct. Original errors are retained; host tests do not establish board PASS.

| ID | Stage | Original observation | Resolution / current state |
|---|---|---|---|
| ENV-001 | startup | sandbox denied SSH network call | Authorized read-only B Ubuntu call succeeded under escalation; no board connection |
| SRC-001 | T01 | New Git checkout changed line endings of 20 frozen files | Confirmed normalized contents equal; restored exact bytes from SHA-verified FIX1 archive, all manifest entries match. Git may show line-ending-only modifications; no logic change |
| ENV-002 | ledger | Git Bash MSYS named-object denied by sandbox | Escalated local sdd-workspace helper succeeded; helper task headings require plan-scoped Python adaptation |
| ENV-003 | T01 tests | initial import missing API; next run temp cleanup denied | These are setup errors, not functional RED. Tests now use candidate build/demo/tmp; separate red-functional run records seven NotImplementedError failures |
| ENV-004 | B preflight | pkg-config json-c development package unavailable | Native codec compile pending isolated dependency discovery; no global installation |
| BASE-001 | protected | S8 real first case natural0/raw127273 but missing SUBMIT_ENTER | Historical failure remains immutable; FIX1 board retest NOT_RUN |
| BASE-002 | protected | Old Mosaic native G1 layer16 exceeded frozen threshold | Not fixed or promoted by new source admission; T05 must independently reproduce |
| DEP-001 | T02 | Ubuntu json-c0.12.1 lacks full unsigned64 API | Isolated official json-c0.18 selected for exact UINT64_MAX codec; source URL/hash and actual native build logs retained. Matching target library remains a T19/T20 requirement |
| DEP-002 | T02 | Archive policy rejected official tests/*.test symlinks | Inspected members; attempt2 materializes only fixed in-archive test_basic.test links as ordinary bytes; no filesystem symlinks created |
| DEP-003 | T02 | Isolated CMake first lacked libcurl, then ldd identified libuv | Exact packages unpacked locally; attempt4 native json-c build natural0. No global installation |
| TOOL-001 | T02 | Transfer helper requested nonexistent tests/__init__.py; caller then used stale four-file payload | Added explicit parent package; archive/hash transport now fails closed before dependent execution; old run preserved |
| TOOL-002 | T02 | PowerShell nested Python quote prevented helper generation | Replaced helper text with literal PowerShell replacement; independent run retained |
| TEST-001 | T02 | Truncation assertion removed only final newline, leaving valid JSON | Test now removes final non-whitespace byte; parser correctly accepted valid original. No production relaxation |
| RPC-001 | T03 | base64 request appended to initial `{}`; permissive initial parse consumed only the empty object | Decode now clears its output and CLI strictly consumes all input. Bounded stdin transport avoids Windows/Unix argv limits. Original stage rejection and probe preserved |
| DEP-004 | T03 | json-c0.18 strict parser rejects legitimate UTF8 on signed-char hosts | Actual line681 compared signed char <=0x1f. Exact unsigned comparison patch recorded; native library in separate vxl1 prefix. Invalid UTF8 and unescaped ASCII controls still rejected in actual local-RPC tests. Same patch required in target recipe |

T03 status: native owner/state/store framework and local-only IPC verified. Application, display and export adapters intentionally return ENOSYS until their dependent tasks/T16 are implemented; this is not a working inference deployment.

## T05 numerical gate

- TOOL-003: old last-layer reproduction computed and saved both outputs/coordinate trace, then NumPy bool JSON serialization exited1. Original attempt retained. Saved-output-only audit reproduced old max0.14232635498046875/RMSE0.0169409764353919, confirmed frozen-input last layer exact0 error. Missing original timing remains null. Serializer now explicitly converts NumPy scalar values; no convolution retry was used for the audit.
- NUM-001: COMPENSATED_TWO_FLOAT_FP32_STATS_V1, first real256×256 input, full propagated16 layers. First15 passed layer thresholds; stage15/index15 failed with5703 violations, max0.1528167724609375 andRMSE0.016617360786211052, host12.797s. Naturalexit1; failed-input.bin/failed-output.bin preserved. Candidate stopped, not promoted to program12/image. No scale, model or threshold change; no new GPU cycles.
- T05 remains OPEN_NUMERIC_NOT_REPAIRED. T06–T12 production acceptance and full dual-app image cannot pass this gate. Approved independent A/C work continues; norm cause analysis remains evidence, not a declared fix.

No GPU raw cycles have been produced by this candidate.

## T14 display errors

- BUILD-001: publication RED helper first failed to link because Makefile prerequisite variables were defined too late. This is a build failure, not functional RED. Separate functional-red run then observed the expected missing-read-result assertion. Both retained.
- DISP-001: role-red run compiled naturally then aborted at test_publish.c:25: an output SHA was accepted as INPUT. Publisher now checks exact artifact role, plus COMPARE/HUNet input source; role-green and final ASan/UBSan pass. Compute result remains immutable.
- DISP-002: installed variable font defaulted to Thin100. Visual inspection motivated explicit Regular400; original assets archived before regeneration. This is display-only, no model/tensor/logits change.
- DISP-003: 640x480 four-digit class/score overflow caused functional assertion in small-layout-red. Split class and score into two lines; final native ASan/UBSan passes this boundary.
- TOOL-004: some development wrapper summaries inherited an inaccurate RED label after successful make. Natural exits, native semantic markers and raw logs are authoritative; no failed build is counted as functional RED.

## T15/T17/T18 continued errors

- DISP-004: fake mode-fallback red rejected a1x1 published frame on3x2 scanout. Backend now letterboxes a display copy and records separate original frame_sha and XRGB scanout_sha/pitch; current ordinary/SAN pass.
- STORE-001: two separately opened store instances duplicated event sequence; functional assertion viewer.sequence==sequence+2 failed. Appends now flock the journal, read the actual durable tail under lock, append/fsync and refresh cached sequence. Shared open readers also lock. Current ordinary/SAN pass.
- ENV-005: old Makefile prepends ./ to OUT, causing absolute output path failure. Next run exposed test_build_edges hardcoded default build. Resolved by independent full source snapshots with default OUT, no old test change.
- ENV-006: Ubuntu Python3.6 lacks capture_output/text. Reused frozen scripts/s8/py36compat per-process only.
- FIXTURE-001: old regression lacked real program6 code then old tasks/weights/input/GELU capture siblings. Added SHA-bound frozen historical inputs in separate run snapshots. These inputs are not new cross-build or board evidence. Prior natural exits/assertions retained.
- TEST-002: Makefile.s8 sanitizer target can reuse earlier unsanitized objects when invoked after normal suite. Do not infer all executables instrumented from target name; a fresh clean source snapshot with explicit sanitizer flags and ELF instrumentation audit is required.
- TOOL-005: one copied development helper printed an overbroad OLD_INTERFACES_PASS summary after running command0 only. The scope is current components; original stdout is preserved, command manifest and natural outcomes govern.
- PROCESS-001: initial IPC code was written before its RED checkpoint. This is a recorded process deviation; existing hostile-reference/UID/long-message and sanitizer tests remain real, not fabricated RED.


## T22 fresh branch review and repair

- REVIEW-001: explicit identical retry after pre-start owner EBUSY returned0 but never reacquired/started. Functional RED `review-red1`; retry is now permitted only in in-memory STAGED before START_INTENT; RUNNING retries remain idempotent. Request/STAGED records are retained.
- REVIEW-002: HUNet classification accepted a different actual tensor than its decoded top-k/logits identity. Same RED run; actual SHA now must equal decoded logits_sha before any result mutation; wrong tensor ESTALE leaves output intact.
- REVIEW-003: OPEN/SUBMIT display errors bypassed durable DISPLAY_FAILED. The presentation loop is now a separately injectable function; all failure phases save immutable run/frame/compute SHA, original errno, phase, monotonic receipt time, raw_gpu_cycles=null and append DISPLAY_FAILED. First `display-red` was a test feature-macro compile failure (not functional RED); `display-red2` then failed the intended missing-journal assertion. Master EBUSY, unsupported capabilities, flip EIO and observer EPIPE are tested with fake KMS only.
- REVIEW-004: enum strcmp accepted embedded NUL suffixes. Functional RED retained; comparisons now require exact JSON byte length. This keeps native/Python identity contracts consistent.
- REVIEW-005: reusing a COMPARE canvas for another RESULT retained the old input_source_sha. Functional RED retained; old input identity now clears on every successful composition before current input assignment.
- Review-green: natural0, current ordinary+ASan/UBSan suite and native viewer build, log SHA a560f17a14f62cd70796deb7c1bd360042c3cad39c8cbff7aa34dab4aba1aced. The same independent reviewer checked the fixes, with no new Critical/Important findings. No actual display execution.
- T15 OPEN: native shutdown safely disables its CRTC but does not yet restore the previous producer's display state; this remains a specification gap, not an accepted deployment exception.
- FIXTURE-003: legacy8 lacked transferred kernel/cnn manifest/binaries. Added exact tracked frozen kernel inputs to full transfer; legacy9 then passed old backend/HUNet/CNN checks but stopped on hybrid's missing sibling release/source.tar.gz. Subsequent run uses private ordinary copies of the existing historical release and exact capture manifest, with preflight; no old tests or source release modified.

Resource scope: fresh Ubuntu read at2026-10-18T17:24:57Z showed MemAvailable11084504KiB and free disk10582204416bytes. No cleanup/deletion was required or performed. This is guest free space, not Windows-host disk compaction. C's processes/caches and all field devices remain untouched.


- CROSS-001: actual AArch64 json-c build compiled all objects, then static archive indexing failed because the frozen SDK prefix has no `aarch64-xilinx-linux-ranlib` wrapper. `link.txt`, CMake cache and tool directory prove the missing path. An isolated per-build ranlib adapter invokes the same target GNU ar with `s`, records its hash, and does not change/global-install the SDK or use a host archiver. Fresh cross2 retains cross1 untouched.
- TEST-003: fresh explicit S8 ASan/UBSan suite natural0 (log7ba2042c7ba5b674baf5cbe33a4cb55c4001aaf6363cb7825067e30f1a2b2e6d), but the extra artifact auditor initially required UBSan handler symbols in every ELF and returned1. Actual22 ELFs all contain ASan initialization;21 contain UBSan handlers. The remaining test_uapi consists only of compile-time static assertions and a constant puts. Saved-source SHA, actual compile line with both sanitizers and artifact hashes were rechecked without rerunning tests; saved-artifact-reaudit records this precise exception and original audit failure. No runtime sanitizer finding was suppressed.

- CROSS-002: cross2's safe manual archive extraction did not preserve upstream file mtimes; libdrm make therefore invoked maintainer aclocal and failed on unavailable xorg-macros. Cross3 preserves each member's release timestamp (same bytes/hash/modes), uses the distributed configure and builds all three AArch64 component ELFs without adding global autoconf dependencies.
- CROSS-003: after the three ELFs compiled in cross3, old rootfs dependency inspection stopped on legacy `build_common` sibling import. Cross4 binds the exact existing scripts/hunet directory for that import, includes all scripts in its before/after source identity, and leaves old helpers unchanged. Cross3 raw logs and compiled outputs remain retained; no full-cross/image success inferred from compilation alone.

- FIXTURE-004: legacy10 completed hybrid checks then opt stopped on missing transferred delivery/templates/opt-crypto.cfg. Transfer now includes all three frozen opt templates. Preflight also caught that old mutation tests execute OPT_CXX as one path; a per-run executable delegates to the unchanged GCC7 compatibility adapter. Neither old tests nor global SDK were modified.
- ENV-007: local Windows CIM memory query was denied; printed derived zeros are invalid and not used. Read-only GlobalMemoryStatusEx succeeded: total16498810880bytes, available5969981440bytes, load63percent at2026-10-18T17:40:04Z. No process termination, cache clearing or deletion performed.

## 2026-10-19 resumed investigation and DP restoration

- NUM-003: normalization causal substitution on A CUDA/cuDNN made all16 outputs bit-exact while keeping real upstream propagation, directINT64, quantization and restore unchanged. This is diagnostic evidence only, not a portable/board PASS. Same captured cuDNN stats with alternative FP32 application orders still differ; FMA alone is not established as the cause. Scripts, stats and outputs preserved in T05/investigation-20261019.
- NUM-004: gamma-first single-variable diagnostic failed atstage15, max0.1471099853515625/RMSE0.016208876298801925,5299violations. Not applied to production. Reused historical target sequential FP32 normalization also failed atstage15, max0.1525421142578125/RMSE0.01661289388332493,5706violations,natural1. All failed inputs/outputs retained; no scale/reference/threshold change.
- DISP-005: original native_close path disabled CRTC rather than restoring prior fb/coords/mode/connector. FunctionalRED natural2 log15e7530db4a8592f20b01c276ba5642bf371f9c2d0c8af8d7060648c6c263e28. Snapshot current encoder route and full original CRTC; reject other output/clone or unknown topology before mutation. Initial GREEN retained independently.
- DISP-006: independent review found initial modeset failure followed by restoration failure could cause another restore on process exit. First-error latch added in native_disable; subsequent close returns original errno with zero extra modesets and pinned maps/fd. Actual native+viewer combination functionalRED2 natural2 logffb1b0c19f96e7add136385e74140afd39003434fbf18e7896701837c9b2cf12; final ordinary/SAN/viewer natural0 log2fc6ac55241f7518330766404a026345114bc3812759de54281aa29eb4b48d1a. Reviewer closed the soleImportant. No physical DRM execution.
- TEST-004: retry-negative first attempt failed to compile because test open/close macros renamed dp_ops fields before viewer.c inclusion. Include public header before macros; retain compile failure separately, do not call it functionalRED. Followingred2 reached the intended dp_close assertion.
- TOOL-006: one Windows inline Python diagnostic generator failed on shell quoting, then its dependent invocation reported missing script. No numeric worker started in those calls. Replaced by a saved Python generator; following historical-sequential-process preserves actual natural1 and raw output. An earlier MXC sandbox launcher also failed while enumerating an unrecognized H: filesystem before running rg; subsequent bounded read-only searches used the authorized normal account. No format or media repair was performed.
- ENV-008: initial A diagnostic resource assertion did not persist its initial numerical snapshot; a later resource-refusal.json contains only a later snapshot and must not be cited as the exact first failure measurement. Successful environment-preflight/environment records bind subsequent actual runs.

Weights receipt: oldselected/native16/A2selected are the same logical1674432weights and coefficients; both original layouts/hashes retained. No production numerical repair or new Vortex cycles. B raw errors, exact per-phase sources and final AArch64 components copied to A,82files SHA verified, archivef7a649f2c316b4bc9fa9873c8d8a50a317293d07e0c18b60742657926e67059e.

- DISP-007: an inactive DP could select an encoder currently routed to another output if that encoder advertised a second freeCRTC. Actual native functionalRED natural2, logb1ea9aa5526662854e6120cf6a5a828411b6dc06217cf9a7cfc30eb040db2093. Fallback now skips encoder.crtc_id!=0, preserving current-route branch. Independent static review agrees; final GREEN/CROSS PENDING because subsequent source SCP failed duringSSH banner exchange, followed by read-only guestprobe exit255. Do not reuse prior ee7ae22... source validation as proof of the final two-line change. New candidate sources saved, no device operation/restart attempted.

## 2026-10-19 native8 continuation errors and closures

- ENV-009: guestSSH repeatedly timed out. User terminal independently proved ens33DOWN/unmanaged with SSHactive and noIPv4. ExistingNIC temporarilyup+DHCP restored129; liveSSH and fresh UTF8/SHA roundtrip passed. No permanentconfig edit/VMrestart. Old255receipts retained.
- ASSET-001: genuine PythonAPI RED8failures and Cvalidpackage assertionRED retained; implemented independent versioned package. Config/mosaic missing from component identity produced an additional functionalRED before source identity fix. Externalassetheader make regeneration functionalRED2 fixed with explicit externalfixture rule. Final10Python+realC ordinary/SAN passes, log3f43dfe38fd1949243020c757fff179059bc4e0197e07e0cad57ca461e05d345. Newpackage is not numerical acceptance.
- CONTRACT-001: first scaffold raised NotImplementedError (testerrors, NOT functionalRED); independent wire14assertions and graph/layout2assertions then failed as intended. Actual C layoutRED2 logdfa8321155c4a4782f0803accd9591367dcc84a033409e08b8f4da202aa87200; CjobRED2 loga590f5ea8d15e32440ffb25c7e9f7bcbe53b48468f2e110d8698bd4e6197ec87. Nativeasset-offset functionalRED also retained. Finaljoint ordinary/SAN10asset+8contractPython and2Ctargets pass, logff56c1df1d47f0df9b27fa194321fd8edec11ae7748be29aa582c64dcf097ec9.
- TILE-001: unpackRED2 log023f2492e331c812e7c4f8d878c8d07c99f25bea277ac5a3d8d0106ade6ca7b2 then executeRED2 log2405ecb6ffb5a1571da02ca5f7c74a91d020ae6c4c4c7cedb3acdba001df6a6d. Implemented one-drain/one-release bounded protocol; final ordinary/SAN exactinteger and9faults pass, log5ba29336778be46742eb858feda43982050fc8f47f7c45277bc4717fd804eb8a. Faultcases are software injection, not actual GPU faults. Output sentinel preserved.
- DISP-007 closure: final8b82source ordinary/SAN log5032555341bf8ac18d8875d0b65b7d84f89ebab25147c9b8ad8f214e505e582b and matchingactualcomponentcross pass. OriginalbusyencoderRED remains intact.
- TOOL-007: a read-only rg command used Bash brace glob inPowerShell and failed parser before execution. Corrected to normal directory search, no implementation/test/device side effect.

## 2026-10-19 T09 propagation and cross issues

- GRAPH-001: independent PACK/norm missing implementations produced actualassertionRED2; RNEquantRED2 log07e65b4031a1dc5f54e1ee5de06baaa1412463c8499eb260c39e1dfdc107ea69. No numericalthreshold changes. Quant/PACK/norm ordinary/SAN log62dc62a59e44d788fb8f80f5f7171d15eecd2cc2c1cd71df94259563baea5825 passes.
- MODEL-001: repeatedtile functionalRED2 logd4b4f6dc453e3d98f65f9546b4b7b2ca3aae5bb518fa5d2915a8cf65d1f2fd25 revealed test-onlyfake reused completed state. Reset per-tileaccepted/bitmap/polls/activity onBEGIN; retain cumulativecounters and refusebeginwhileactive. Productiontile unchanged. Ordinary/SAN log519c31dabb38b91be33d6046c125e66fbb058a59e77b6777db167baa6d6ebf8c passes.
- GRAPH-002: fullgraphmissing-runner assertionRED2 logd5979a0ce529bdee0c77757ad8bf6c9705bc9cdad7b8eed4363b243ca0e6e3a6. Real12x8weights/newpropagation thenall16bitexact, ordinary/SAN3b14e99f3e6aea6a12b1ca474fe0ceae7eb88d6fa2cd75085e5b091598b98dad. ExpandedC/Python/fullmetadata/role/owner tests pass84484b08dd4b82a09b59071ac67ff1084af4971f04e71d8b8cf9d5c3b8cfba10.
- CROSS-004: RV32 graph objectcompile failed because mosaic_wire→vxl/core→LinuxUAPI required linux/types.h inbaremetal SDK. Retainedstderr/compilelog andsource02c284.... Introduced independentmosaic_types.h and keptpublicLinuxABI unchanged; all8actualobjects passcross2. This wasnot anRTLfailure.
- BUILD-001: aggregateaftergraphheaderintegration failed standalonePACK compile due missing-Iinclude, natural2 logfc5ea2eba3ef810289a4cc850d323fc6f1c5a144eb2b2c11e49f90bd8c1277c2; corrected only targetinclude paths and addedprivateheader prerequisites. Freshfinal2 aggregate ordinary/SAN0 log43024d02a30beb6e66a658b79da3316b222b9f4d8402f5580c6ad26cf35b2a3c. Failedrun untouched.


## 2026-10-19 T10 lifecycle and batching
- STATE-001: missing phase/lifecycle RED2 fc49f350...; trusted model GREEN ordinary/SAN cebd6ca9.... Not engine admission.
- STATE-002: quarantine blocked error-evidence delivery, functional RED2 94b1f56d.... Allow only same-owner evidence prefix delivery while execution and REAP stay blocked; ordinary/SAN GREEN66552f52.... Final per-fault output/READY-entry test11963aa9... natural0.
- GRAPH-003: bounded-host-scheduler ignored requested batch3; functional RED2 01f89dd2.... Range splitting retains each phase barrier and exact output; joint ordinary/SAN99acb90a... natural0.
- HARNESS-001: four old-S8 regression startup failures, 645b0de6... and0a874159... (OUT reaches fixed-path make -pn checker through overrides and environment);41d44642... (Ubuntu GCC lacks ffile-prefix-map);eefb4358... (./ plus absolute output path). No old source changes. Relative independent OUT, clean Python child make environment, existing GCC-compatible numerical flags: natural0 d7948149.... Failedruns remain separate.
- TOOL-008: apply_patch rejected a patch that deleted and added the same path in one request; no files changed. Reissued as a normal update, then real Ubuntu tests. This was not a product or numerical failure.


## 2026-10-19 T10 real driver/loader
- RUNTIME-001: internal evidence memset erroneously used external512 and cleared adjacent I/O callbacks: crash30a1fbf4..., precise RED58ec6852..., restored sizeof internal structure. Ordinary/SAN GREEN388ed7fa....
- RUNTIME-002: nonfinite input and BEGIN DMA failure admission RED01c7dec4..., added integer FP32 bit checks and fault/pin before GPU START; ordinary/SAN661c7da0... passes.
- DELIVERY-001: actual ioctl early input-copy fault not latched RED1189db10...; locked same-owner session fault preserves firstEFAULT; actual wrapper ordinary/SAN21fd18a2... passes.
- REGRESSION-001: old unknown6 assertion RED3f9f2a19... conflicts with approved Mosaic6; unknown7 and explicit unbound6/old5-newcommand/program negatives added; old four suites plus actual Mosaic driver42f236ca... pass.
- BUILD-002: missing mock dependency1ed9b56d... was link failure, not functionalRED. Added test dependency; actual functionalRED7d4f48a7.... Engine strict indentation build31d8a22e... retained and corrected without disablingWerror.

## 2026-10-19 resident loader / profile coordinator / real adapter continuation

See `RUNTIME_RECEIPTS_20261019.json` for exact natural exits, raw-log hashes and independent paths. Previous errors above remain unchanged.

- BUILD-024: HUNet observer first hit old GCC unsupported file-prefix flag, then source transfer omitted tools headers, then historical fixture prerequisites were absent. Separate attempts preserved; test transfer now includes actual tools and hunet_fused test sources, historical program/weights verified before extraction. Strict numeric/warning flags retained. Functional observer RED32180c01 -> GREENd5c99bf5; observer can fail before REAP/next START.
- BUILD-025: new runner linked without conditional fixture helpers; source transfer omitted tests/hunet_fused. RED setup log preserved, then functional init-ENOSYS RED163a997d obtained. HUNet actual program9 protocol client + CPU GELU adapter ordinarybc446515 / sanitizer2a4ced2f passed; this model does not emulate GPU tensor math.
- BUILD-026: strict misleading-indentation warning in new adapter, natural2/log9dbf1653; statements split, no warning disabled. Default diagnostics remainfalse.
- AUDIT-006: software client accepted missing SUBMIT_ENTER, functional RED77d9349a. Added nonzero entry, status DONE/ERROR, CDMA error, reserved and exact drained flags. First patch incorrectly required status==2; normal0x502 rejected (a95bb95e); corrected to actual bit semantics, GREENf09e5741. Every damaged512B record retained before rejection; no REAP or next START. This does not alter original S8 missing-entry failure or its127273cycles.
- RPC-006: synchronous start callbacks could outlast5s client wait. Durable enqueue and single asynchronous worker now preserve request/STAGED before execution, reconnect dedup and busy-switch exclusion. Functional enqueue RED -> GREENa55b77c0; runtime-control RED ->8tests GREEN5ca5964a. Delayed callbacks are explicitly software-only; default production provider ENODATA, no model fallback.
- DISP-017: new display restore test missing errno header; setup failure retained, then functional ENOSYS RED observed. Read-only reconstruction validates exact published result and all loaded artifact hashes before INPUT/RESULT/COMPARE composition; GREENcc92c96e, corruption rejected.
- DISP-018: new IPC test parent aborted on expected stub failure; test child retained stdout while blocked in accept. Exact own childPID39059/executable/args/status saved; only that orphan receivedSIGTERM. Added5s child alarm. Production IPC was then correctly rejecting test socket0775; actual mode/uid recorded. Test now creates0600 like real viewer, rather than relaxing client peer/path rules. GREEN4a11fa6c. A strict cleanup-line warning045ee57d was also preserved. No C or device process was touched.
- ABI-006: plan review found new Mosaic convenience wrapper returning only start status; aligned its private interface with approved `(session,run,result*)`, matching HUNet. Public176B UAPI and old program IDs unaffected. This final small change requires affected fresh regression/cross results before current-head claims.

New error JSON fields distinguish function `library_errno` from process `natural_exit:null`; old failed records with negative errno in natural_exit are retained, not corrected in place. Raw result and GPU receipts remain authoritative. No new board/DP evidence or real GPU counters.

- OWNER-004: final positive wiring review reproduced repeated init overwriting an installed runtime (natural2/log42e1d20d). Both runtime and HUNet adapter now reject reinit with EBUSY; successful destroy detaches callbacks and clears private installation only after trusted release. Ordinary644f217f and ASan/UBSan97cd2188 pass. Failure leaves old state intact; no reset or force-free.

Final complete ordinary13-target regression9b8d0e16 and address/undefined sanitizerf855e2a6 natural0; subsequent approved Mosaic result-output parameter alignment and disabled-model timing assertion pass ordinary/SAN576b0853, with B controller literal-selection tests. Latest real AArch64 unbound runtime ELF SHAa7b4425c64d13fa5dd4e84296f31c4c253e963474662aafc70a880137ceca557,3717240B, no model macro, source unchanged during build and target rootfs versions verified. This is not final source-bound catalog/kernel/image or board PASS.

## 2026-10-20 数值根因续查

- RESOURCE-020-001: A RAM3837071360B低于4GiB，任务在数值计算前自然1；真实账户后查xsim34948/xsimk25904属于其他任务，未终止。另一次只读CIM查询受账户权限拒绝，未降低资源门。
- BUILD-020-001: 局部诊断C++同一行if/声明触发-Werror=misleading-indentation，自然1；拆行后独立green编译/运行0，未关闭告警。
- NORM-020-001: 首层保存cuDNN统计量+gamma-first/fmaf可bitexact，但当前统计量+相同fmaf的真实全链仍末层失败，自然1，原max0.1/RMSE0.01门不变。失败输出97244e50...，全部16层原始结果留证，未进生产。
- NORM-020-002: 典型分块FP32/Welford统计探针60项无完整cuDNN统计量匹配，属于诊断未闭合，不称编译故障或可移植不可能证明。
- 原始归档30695522B/SHA2dc8273039a10f0c554caabc7f5da735337047e8bbb64c2f25360e8caab338e6，A/B双份、69成员核验。gpu_raw_cycles=null；原S8失败SHA及127273周期未改，未连接板卡或SD。

## 2026-10-20 IN独立兼容候选续查错误账本

- RESOURCE-020-002: PyTorch型标量探针在4GiB预算门处拒绝；未开始CUDA计算。改为不导入Torch的driver/NVRTC小探针，另存普通单测预算，不降低整网/镜像门。 证据：`evidence\demo\T05\investigation-20261020\rtx-scalar-rcp`。
- RESOURCE-020-003: WDDM把桌面进程标C+G，空compute-apps门误拒绝，未创建CUDA context。保存原列表并核对DWM/Taskmgr；改用实际利用率/空闲显存与固定缓冲预算，不结束其他进程。 证据：`evidence\demo\T05\investigation-20261020\rtx-driver-scalar-rcp`。
- BUILD-020-002: NVRTC找不到nvrtc-builtins DLL，编译失败未启动GPU。仅在子进程PATH及显式预载补全同版本依赖，v3成功，未改系统安装。 证据：`evidence\demo\T05\investigation-20261020\rtx-driver-scalar-rcp-v2`。
- BUILD-020-003: 旧分步表达式未满足独立FMA舍入见证，保留功能RED；v1显式FMA单测GREEN不代表整网通过。 证据：`evidence\demo\T05\investigation-20261020\b-compat-unit-red`。
- NORM-020-003: 真实256全链v1末层max_abs0.1569976806640625/RMSE0.016023627569399696，自然1；原阈值不变、16层输出保留。 证据：`evidence\demo\T05\investigation-20261020\b-full-compat-v1`。
- TRANSPORT-020-001: WinError206本机命令行过长，SSH未启动；改为SCP传小脚本、核对SHA后用短命令执行，独立file目录保留原失败。 证据：`evidence\demo\T05\investigation-20261020\b-weighted-welford-rcp-probe`。
- NORM-020-004: v1倒数根与原参考原语相差1ULP，闭式见证RED；模型无关全域映射独立记录，不使用黄金层统计。 证据：`evidence\demo\T05\investigation-20261020\b-compat-v2-unit-red`。
- TEST-020-001: 初版测试把数学RN预期当作硬件近似RSQ值；独立全域采样显示实际0x3f7ffffe，修正测试预期，保留错误目录；未改阈值。 证据：`evidence\demo\T05\investigation-20261020\b-compat-v2-unit-green`。
- NORM-020-005: v2前两层bitexact，第三层首次差异；末层max_abs0.1251068115234375/RMSE0.014203833595331252，自然1。固定输入32/128/512线程探针均未匹配，停止盲猜。 证据：`evidence\demo\T05\investigation-20261020\b-full-compat-v2`。
- BUILD-020-004: 新增v3两处同一行if/赋值触发-Werror=misleading-indentation，自然1；拆行后独立green2编译/运行0，未关闭告警。 证据：`evidence\demo\T05\investigation-20261020\b-singleread-green`。
- NORM-020-006: v2固定第三层159552项bit差异为功能RED；实际CUDA trace与SASS确认single-read的统计及仿射顺序，新v3固定输入0项差异。 证据：`evidence\demo\T05\investigation-20261020\b-singleread-red`。

完整来源指针见`docs/mosaic/NORM_ERRORS_20261020.json`。CUDA静态工具首次缺nvdisasm PATH、nvidia-smi XML无效进程名编码、局部PowerShell嵌套引号SyntaxError均为诊断/编排问题，失败文件保留，未提升为算法失败或板端故障。仅新的成功run单独报告，不覆盖原失败。

## 2026-10-20 正式21图与最终工具链收尾错误

- INPUT-020-001: 末张正式应用输入不在native输入目录，初次准备失败、未传输或运行。找到原正式run的original_input.bin（14745600B，SHA11a8b47e3bc69123ac8a09e7e65150cb1b916ef60d612a9a702e9980f9e22063），独立retry使用原文件；没有重新预处理。 原始证据：`evidence/demo/T05/investigation-20261020/formal-v3-input-20`。
- AUDIT-020-001: 审计初版仅检查数量，功能RED有7项失败；实现逐row/层/哈希/阈值/退出/输入身份及伪GPU周期拒绝，8项测试GREEN。原RED及失败helper保留。 原始证据：`evidence/demo/T05/investigation-20261020/audit-unit-red`。
- BUILD-020-005: 普通/SAN/8审计测试已通过，但RV32数据.S编译经glibc包装启动器的子预处理器失败，自然1。未改编译器或系统；独立green2直接使用同clang的-cc1as RV32/ilp32f，验证ELF/ABI/符号容量后自然0。 原始证据：`evidence/demo/T05/investigation-20261020/b-final-norm-verification`。

上述失败现场保持失败；独立成功run不覆盖原始记录。

- INVOKE-020-002: A最后补跑审计从知识库根目录启动，import scripts失败，未执行数值计算。保留独立错误收据；从实际工程根目录运行同一8测试自然0，无源码/阈值变化。 证据：`evidence/demo/T05/investigation-20261020/a-final-audit-wrong-cwd`。

## 2026-10-20 T19入口、FCSR与只读表

- T19-INVOKE-001：单测编排缺少应复用旧C源，未开始有效功能测试；red2修正源路径后保存缺失入口/sidecar API的RED。 原始目录 `evidence/demo/T19/entry-rodata-red-20261020`。
- T19-FIXTURE-001：错误使用两通道简化资产构造完整模型registry，初始化断言失败；不作sidecar功能RED。改为既有3382784B完整几何fixture，在red2复现缺失sidecar篡改检查。 原始目录 `evidence/demo/T19/rodata-runtime-red-20261020`。
- T19-NEGATIVE-001：实际seal在sidecar已损坏时仍成功，功能RED自然SIGABRT；补齐真实client/code-span/seal/immutable检查后普通/SAN均0。 原始目录 `evidence/demo/T19/rodata-runtime-red2-20261020`。
- T19-BUILD-001：输入快照未带norm-compat-v3.json，编译前缺文件；独立attempt2补齐，原run保留。 原始目录 `evidence/demo/T19/full-rv32-attempt1-20261020`。
- T19-BUILD-002：把未使用的Linux主机range.c编入裸机，缺linux/types.h；仅从RV32输入删去该无依赖单元，公开ABI不变。 原始目录 `evidence/demo/T19/full-rv32-attempt2-20261020`。
- T19-BUILD-003：带默认成员初始化的C++上下文不能直接声明__thread；改用CRT清零的对齐TLS存储，回调中placement构造，不加入动态TLS运行库。 原始目录 `evidence/demo/T19/full-rv32-attempt3-20261020`。
- T19-BUILD-004：冻结libm依赖__errno但最小libc未提供；实现真实hart-local errno accessor，attempt5完整链接0。 原始目录 `evidence/demo/T19/full-rv32-attempt4-20261020`。
- T19-INVOKE-002：A的一行Python复制CRT时PowerShell嵌套引号导致SyntaxError；没有写成源文件。改为独立prepare_mosaic_crt_files.py，无工具链更改。 原始目录 `evidence/demo/T19/runtime-contract-green-20261020`。
