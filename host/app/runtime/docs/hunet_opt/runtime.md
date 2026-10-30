# program10 sealed runtime

This software candidate reuses the frozen W4 bit/XSA. There is no board operation in the offline suite.

The compiled catalog binds actual GPU jobs, 13 CPU GELU positions, 1023 logical jobs and 100 original stages. GPU completion advances one physical job and its authenticated logical range only after exact evidence delivery. Graph17 virtual slices/concat are not captured as materialized tensors. Final progress, hardware job delta, guards, owner map, CRT, drain/idle, input and finite logits remain mandatory.

Default diagnostics are off. No-argument CLI prints help. `explore` requires explicit exploration authority and always remains numeric-unreviewed; it cannot create a formal performance PASS. `measured` retains a separate exact-binding/ROI numeric and human authority gate. Diagnostic capture, CPU single-thread reference and writeback capture run only with explicit diagnostic mode. Raw GPU cycles stay distinct from Linux intervals.

The candidate layout binds SHA/load/control/GN policy. C1_T retains portable SHA and old GPU wire; C1_H changes SHA/load but retains old GPU wire. G1 changes GN; G2 variants change only their matrix path; G3 graph closure uses parallel GN; G4 chunk variants change remaining operators. Unneeded control/scratch pages are not initialized per job. Actual compile flags and wire identity are independently checked at cross-build.

Kernel and user SHA observations retain actual backend implementation, bytes, failures and fallback count. Kernel audit is generation-scoped and exported read-only beside management transactions. An algorithm-name change sets an explicit integrity flag. Neither host portable fallback nor ISA emulation proves A53 SHA2 execution.

Owner validation derives original-tile ownership from the exact multi-tile group distribution. Tests compare it with an independent group scheduling oracle. Graph17 concatenates the two materialized matrix traces; virtual nodes add no fake GPU starts.

The CLI, library and data install locations are `/usr/bin/vxl_hunet_opt`, `/usr/lib/libvortex-opt.so`, `/usr/share/codex-hunet-opt`. Deployment installation belongs to the audited image packaging stage, not native tests. Target AArch64 cannot link the host-only SHA2-unavailable stub.
# Snapshot lifetime and audit records (final review correction)

The CLI persists management.csv, submit.csv and kernel-digest.csv before closing
the owner fd. Controller driver-capture.json must hash these exact saved files
and bind the same boot ID, run ID and generation. It must not replace them with
a digest export read after close or after another owner opens the device.

Management storage remains bounded at512 rows. A new successful owner open can
recycle only a fully serialized, healthy prior table. Active, unsnapshotted,
overflowed or failed tables are refused and retained. Driver open never clears
such failures. Old raw snapshots are permanent run artifacts; live debugfs rows
are an epoch buffer, not a persistent archive. The submit buffer and digest
counters start a new epoch together. Audit parses the full CSV schema/global
overflow first, then selects the bound generation without rewriting raw files.

load-timing.bin and user-digest-audit.bin now have a16-byte little-endian envelope
(magic,version1,total bytes,reserved0), followed by the source-bound native LP64
record (2616 and392 bytes respectively). Missing or nonzero status/overflow,
bad padding, incomplete asset coverage, failed digests and timing overrun reject
audit. Load partitions are exclusive; kernel management/CDMA measurements remain
nested rather than being added again. Backend names/fallback counts are observed
identity evidence and cannot establish a speedup.

cpu/libm.sha256 is a mandatory65-byte startup asset, equal to the authenticated
runtime libm SHA followed by LF. Earlier T22 packages omitting it are retained
as rejected candidates, and cannot be used for runtime delivery.
