# U16 native execution policy compatibility evidence

P8 source math and catalog/plan validation are host evidence only. Actual native driver binding is still OPEN.

- Frozen 512x512 raster uses (512/4)^2 =16384 tile directory records. driver/v2/execution.c presently caps trusted policy.tile_count at4096; program12 therefore cannot bind faithfully. Need a compiled negative/positive boundary test before extending the generic capacity to the actual frozen maximum; retain per-record and ref-id checks.
- Frozen P8 raw programs end with fence/tmask-zero, not NN CRT per-core exit-word stores. Native execution policy presently requires4 fixed retirement words. Do not invent a CRT, fill it from the CPU, patch old code under old IDs, or promote protocol-only CPU injection into native execution acceptance. Need trace of exact instruction write ownership and matching runtime retirement proof, or a separately versioned generic completion policy justified by actual frozen wrapper DONE/drain behavior. No field operations are authorized here.
- The same assumption must be checked for old image raw programs before U22 boot policy is finalized. U14 module build did not validate final installed program boot binding. Keep this as a real open obligation, not a regression waiver.
- Catalog namespace2/profile5/program9..12 retain exact original code arrays (zero padding to2048 as original upload). S8 and W4 provider identities stay isolated; NN fastwait is not extended to graphics.

GPU raw cycles=null; no board, DP, performance or numeric GPU PASS.

## Follow-up: offline compatibility implemented, physical acceptance still open

The findings above are retained as first evidence. Private execution header2 adds an explicit GLOBAL_DRAIN mode for the frozen no-CRT programs. Header1 and mode0 still require four exit words; CRT/stack-bearing schemas cannot use the new mode. Public UAPI and original program identities/code remain unchanged. This is a software compatibility change against the frozen W4 wrapper, not new RTL or a hardware PASS.

- Native policy030 witnessed compiled functional RED before implementation; actual generated1088byte execution-v2.bin is byte-compared with the four frozen policies, boot-bound and mutated in064. Tiles up to16384 are bounded; canonical directory and ref-ID checks remain.
- The exact frozen six-file wrapper hierarchy and hashes are in U16-wrapper-evidence.json. Source supports global busy/drain/DONE inference; normalized core_mask15 in this explicit mode is not four observed exit stores. Physical retirement still requires later board evidence.
- Owner055 normal13cases,056 ASan/UBSan and058 TSan pass actual common C ownership/loader/queue/ACK with explicitly labelled CPU output injection. No CPU oracle is uploaded by production materialization; the host bridge injects after the fake START and is not a native hardware execution proof. Corrupted color/depth, startup-reference bypass, cancellation, guard, projected-data mismatch, trace/publish/display failures and forged stages are tested.
- Math/native064,068 ASan/UBSan and069 TSan pass; TSan does not execute the single-thread instruction model. Normal and ASan execute original instructions for10 fixed frames plus reverse raster, comparing exact output with independent whole-triangle CPU math. Initial100 is a logical control/reference gate, not100 rendered physical frames.
- Owner065 and five math/policy binaries066 compile with matching AArch64 GCC9.2; rootfs symbol-version/dependency inventory passes. They were not executed as board binaries.
- Matching Linux5.4 module067 compiles with unchanged parent .config/Module.symvers, SHA6ddd580554cb8d925ad321addf4e829821e89fe86cf147f61de0f43437ced16b. This is not final startup/image binding; U22/U23 must audit actual installed policy/catalog/DT/provider and full boot artifacts.
- New output format floor and initial reference guard follow functionalRED050/052; first failures051/053 are preserved. Legacy test059 failed because its full path containedp8;063 retains first log. Explicit argv1S8/argv2P8 test identity fixes that fixture without deleting checks;064/068/069 pass it.
- Frozen theme cm_pack_rgb places R in bits0..7,G8..15,B16..23,A24..31. RGBA8888 bytes on LE are correct; no speculative red/blue swap was introduced.

U17 custom texture transaction, U19 lifetime budgeting, U20 real factories/service, U22/U23 final image binding, whole-branch review and physical GPU/DP/performance remain open. GPU raw cycles=null throughout this offline task.
