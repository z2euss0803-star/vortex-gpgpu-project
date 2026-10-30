# HW4 fused CDMA bounded wait candidate

Approved 2026-10-15: offline implementation, cross compilation and image only; no board or SD.

Only active program9/profile4 GPU submissions use a per-transfer stack policy.
Eligibility uses a mutex-scoped SUBMIT flag or RUNNING job state, independent
of optional trace storage. A full trace buffer still reports its overflow flag
and cannot silently disable the runtime policy or claim complete timing data.
After BTT, allow at most 64 additional short poll yields while elapsed ns <8000.
The initial ordinary status read is not an additional fast yield. The policy is a
bounded polling decision, not a hard realtime scheduling guarantee. On exhausted
time/count, retain usleep_range(50,100). Core status/error/deadline/address checks,
register writes, barriers, guard frequency and GPU program remain unchanged.

CSV V2 appends cdma_fast_poll_ns and cdma_fast_poll_calls before guard_depth.
The ns field measures fast-yield callback time including cpu_relax; core MMIO
reads and loop overhead remain inside cdma_ns. It is not total CPU busy time.
Sleep and fast-yield times are disjoint nested parts of cdma_ns. WAIT sleep and
worker activity overlap GPU execution. Analyzer program9 explicitly requires345
guard CDMA calls/22080B; legacy programs require130/8320B.

Frozen reference:835466086 raw GPU cycles; ORT FAIL8105 retained. Software tests,
image audit, H0 and H1 are separate. A new image is not board performance proof.
