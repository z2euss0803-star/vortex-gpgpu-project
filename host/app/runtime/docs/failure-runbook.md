# Failure and recovery

- Connection failure: identify A→B versus B→Ubuntu; preserve existing strict trust entries and all prior failure logs. Reconnect when B is awake. No board power is necessary for offline work.
- Invalid ioctl/foreign/stale handle: reject before MMIO. Host pointer tests cover validation and injected faults; real Linux uaccess/compat require target evidence.
- COPY partial failure: return negative errno, `used` is a conservative completed prefix. Operation is not atomic. DMA error/deadline quarantines objects and permanent bounce; never clear unknown errors or reset merely to release memory.
- WAIT timeout: query the same job again; it remains RUNNING. Execution deadline worker runs independently after user close. New submit/free/copy are prohibited while pinned.
- GPU FAULT/pending/jobs/ACK inconsistency: retain all session objects and device ownership. H0 has no accepted quiesce/reset path. Recovery is a controlled system/hardware restart under later user authorization; no RESET ioctl.
- Wrong program: real driver compares exact fixed-address vecadd16 words and trailing zero area. Generic CRT/perf/ELF is not approved. The CPU synthetic test payload never counts as a board program.
- Real probe: default fails closed without approved PS placement, reserved-memory binding, real clocks/reset/ready and unique CDMA resource. Review final DTB and boot occupancy before enabling. The current candidate and old SD are untouched.

Next target validation when B is reachable: sync hashed source → `host_all.sh` → `cross_check.sh` → audit interpreter `/lib/ld-linux-aarch64.so.1`, dependencies, identical ABI section and unchanged original .config/Module.symvers. Rebuild after any source revision before claiming OFFLINE_READY. Never substitute previous target logs for a changed final source.

H0 board gate waits separately for reviewed DTS/memory and explicit deployment authorization. H1 waits for one hardware manifest and accepting owner. These are not reasons to fabricate PASS for offline, board or H1.
