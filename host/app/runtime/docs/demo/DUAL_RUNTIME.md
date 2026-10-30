# Resident loaders and serial dual-application runtime

Status: offline component implementation. No image admission, target execution, SD write or physical DRM operation. Four native 8x8 signed INT16 PE arrays remain Mosaic hardware; HUNet remains FP32 program9 with A53 ERF4. Public176B UAPI and old program1..11 unchanged.

## Driver ownership

Only compatible `vortex,hw4-s8-demo-v1` installs the profile factory (default5). SELECT64 accepts explicit4/5/6, exact generation and zero extra fields. Old compatible nodes reject64. Same profile preserves context; changing profile requires trusted quiescence, no pending job/pin/quarantine/unexported evidence and zero objects. A fresh candidate is fully allocated/validated before atomic commit. Failure preserves old context. Ownerfd is not reopened; generation is incremented without wrap.

The first HUNet FREE may retire a program9 seal only on this new node, after full END payload and outer envelope delivery,1023 logical/103GPU/13CPU/100stage completion and trusted hardware idle. Legacy nodes retain sealed-FREE refusal. Faults never obtain a cleanup exception.

## Host adapters

`mosaic_client_attach_fd/load/frame/run/close` borrow owner and journal descriptors. Code and weights load once per selected session; each new frame has a fresh input epoch/run identity and77 phases with at most4096 items per job. Every received512B record is retained before audit; missing SUBMIT_ENTER, invalid CDMA/status/timing/exit/identity refuses REAP and subsequent START.

`demo_mosaic_run(session,run,result*)` and `demo_hunet_run(session,run,result*)` return a completely validated/published result through the ordinary runner. Computation DONE is insufficient for publication. Production scopes must be REAL_BOARD, while separately compiled software tests must be OFFLINE and have raw_cycles=null, cycle_valid=false, timing_valid=false.

HUNet attaches the same borrowed fd, verifies binding/program9, CPU ELF/self/libm/assets, and128B external measured authorization gate. It invokes the unchanged measured graph,103GPUcalls and13CPU GELU functions with no intermediate capture. Its optional per-GPU observer saves the448B original after existing validation and before REAP; observer failure stops the graph. Default old CLI has no observer and unchanged behavior. Historical metrics may be attached only when original probe/logits/input/model digests match and the actual final logits are byte-equal. ORT/R2 original thresholds and failure remain, and the actual new logits are never replaced by historical bytes.

`demo_dual_runtime_init` installs both real adapters and private detach callbacks under one dispatcher. Caller zero-initializes state and retains config/assets. Initialization does not open /dev/vortex0. Reinitializing an installed runtime/adapter returns EBUSY; safe destroy releases owner only through trusted quiet+detach, unregisters callbacks and then drops host state. Faulted owner/state cannot be reset by reinitialization.

## Local daemon and B control

The source-bound release provider is `demo_release_install`; current default provider returns ENODATA. There is no runtime software-model fallback. T19 must generate the actual production provider and final binding after executable build, then compile the fully bound kernel; a prebinding module or unbound daemon is not deployable.

1. Explicit `select-app {app:mosaic|hunet}` acquires owner, reads actual post-open generation and selects profile. B then creates its STAGED run with that returned epoch. No pre-open generation is reused.
2. `run-mosaic`/`run-hunet` durably writes request and STAGED before queuing one background worker. Response QUEUED is acceptance, not success. The worker performs actual start/wait/collect/publication. A client disconnect never closes the owner or launches a second worker.
3. Identical run metadata while queued/running is idempotent. Another run, switch or display during work returns busy. Status reports RUNNING without reading the worker-mutated session; future readiness/get synchronizes completion before session access. Faults retain quarantine and original errno; no automatic retries.
4. An unfinished durable request after daemon restart restores quarantine. No implicit recovery, cancel, profile change or reset is available. Existing completed result can be queried/exported by its own run identity.

Default standalone unbound daemon remains useful for staging and framework tests, but refuses computation. Test-only delayed callback provider is linked into a separate explicitly OFFLINE binary and is excluded from the production cross build.

## Display ownership and evidence

`demo_prepare_display` reconstructs only from an already published exact compute_result.json and verifies all raw output, input RGB and output RGB asset digests. It composes1280x720 INPUT/RESULT/COMPARE with app/scope/numeric/run label. Tampering refuses display and does not rerun inference. Source result SHA, raw output SHA and input display SHA have distinct roles.

`demo_frame_publish` stores the immutable RGB/frame manifest after the compute result exists. The display callback sends only run/frame/source identity to fixed `/run/vxl-demo/dp.sock`; parent/socket permissions and peer UID are checked, transport is bounded10s, and raw request/reply is saved. The viewer alone owns DRM/scanout buffers; IPC completion is not a fabricated DISPLAY_PRESENTED timestamp. A display error cannot change compute results or quarantine/restart GPU work.

Linux monotonic timings, nested driver intervals, raw controller cycles and B network duration remain separate. Model counters1000/77000 and host CPU durations do not establish GPU/board performance. New error records use library_errno with natural_exit:null; true process exits come from command receipts. Failed artifacts and original S8 raw127273 remain immutable.

## Gates still open

Mosaic old-reference stage15 FP32 max0.1528167724609375 / RMSE0.016617360786211052 exceeds frozen0.1/0.01. Its high display-domain PSNR does not waive raw FP32 acceptance. Full21-image/T12 and final T18 admission are open. Final RV32 entry/FCSR initialization, production catalogs/provider/bindings, full T19 and T20 image remain uncompleted. Reused hardware has no new physical verification here. C's processes and caches are untouched; software tasks hold all three coordination locks and use one worker. No cache deletion occurred.
