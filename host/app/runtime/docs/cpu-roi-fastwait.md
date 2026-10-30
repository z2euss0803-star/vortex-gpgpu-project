# CPU/ROI bounded CDMA waiting and timing closure

User approval on 2026-10-15 covers only the four management ioctls
`VXL_HUNET_BEGIN_ROI`, `VXL_HYBRID_CPU_BEGIN`,
`VXL_HYBRID_CPU_COMMIT`, `VXL_HYBRID_END`, with the same owner,
profile4/program9 and READY state. The mutex-scoped flag is cleared after
every engine call, including errors, before copying any result to userspace.
Existing SUBMIT and RUNNING behavior is retained. Other profiles, ordinary
COPY and loading do not acquire this new scope.

Each CDMA transfer reuses the existing 8,000 ns / 64-yield budget and then
falls back to the original 50–100 us sleep. Status, old IOC, idle, barriers,
absolute timeout, full workspace poison, guards, readback/hashes, ownership,
token/generation, delivery idempotency and quarantine are unchanged.

`timing.json` retains its exact legacy `gpu` and `cpu` rows and adds the
explicit `handoff_v1` object. Its version is 1, with ROI begin/end durations
and one CPU detail row per legacy CPU receipt, identified by sequence/stage.
The CPU detail records the complete handler (including vector destruction),
its nested checkpoint time, input/output SHA time, full `hunet_cpu_gelu4`
call and the difference between that call and its existing worker interval.
The worker interval already includes pthread creation and join. Exterior
time therefore covers validation/allocation/copy/free and small timing
overhead; it is not a claim to independently time each of those functions.

Full CPU handler replaces legacy CPU-call sums when closing resident time.
Its own checkpoint time is subtracted once before adding global checkpoint
time. SHA and worker exterior are nested within the handler, not additional
resident intervals. Legacy JSON without this extension retains its original
meaning; unsupported, inconsistent, negative or overlapping extensions fail.
The binary UAPI and 712-byte CPU / 448-byte GPU receipts are unchanged.

The latest direct instruction selects the W4 hardware in
`contracts/selected_hw4_w4_v1.json` for this independent candidate. Original
profiles are retained under `contracts/baselines/` and the original worktree
and releases are unchanged. Model/input/precision and GPU wire bytes remain
frozen. Matching W4 bit/XSA require new source/profile bindings and a rebuilt
BOOT.BIN from matching firmware, with package receipt and actual BOOT bit
payload audit. Do not reuse the parent's old BOOT.BIN for W4.

W4 baremetal ROI0, current Linux software validation, cross compilation,
image audit and future Linux board execution are distinct evidence scopes.
Original W4 12/synthesis/ORT8081 issues and Linux ORT8105 remain open. No board,
SD, reset or PL operation is authorized by this offline change.

Historical GPU raw cycles remain 835571748. Its longest individual old-platform
job is GN_STATS at 93361547 cycles, only 2.334% of the 4000000000-cycle per-job
watchdog. The watchdog counts active controller cycles, including startup
and drain, not instruction steps; it is not a whole-network limit. Hardware
elapsed/watchdog are 32-bit. No watchdog or deadline is increased here.
