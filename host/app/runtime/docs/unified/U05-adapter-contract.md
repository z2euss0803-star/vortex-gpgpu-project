# U05 Linux boundary and evidence scope

The existing 176-byte ABI and old command numbers are unchanged. A device has
either its legacy startup path or one matching compute-v2 provider. No ioctl
switches hardware/profile. Until U06 explicitly binds the startup provider,
`device.compute` is NULL and the new route is inactive.

All queue mutations, snapshot/ACK, completion copies, close and poll observation
share the original device mutex. Input batches are copied once to a bounded
private allocation (maximum 32 * 512 bytes), then validated/pinned atomically.
The worker, not the SUBMIT return path, executes START. Once accepted, an output
copy fault does not roll back or repeat that batch. Cookies retrieve its state;
replaying an accepted cookie returns EALREADY (or ESTALE after explicit retirement).

## Envelope use (128 bytes, commands unchanged)

| Command | Request fields | Result |
| --- | --- | --- |
| SUBMIT_BATCH 0x88 | `length=1..32`, `user_ptr` points to that many 512-byte jobs, `payload_bytes=length*512`; no incoming sequence/cookie/object/offset | First accepted sequence/cookie and QUEUED; the complete batch remains discoverable on return-copy failure |
| WAIT 0x89 | sequence and/or cookie select the same job; `length=0..60000` milliseconds, `user_ptr`/`payload_bytes=256` designate completion output | Completion snapshot, without consuming; ETIMEDOUT/interrupt do not abort work |
| COMPLETE_REAP 0x8a READ | `flags=1`, sequence/cookie, 256-byte output; no length/object/offset | Repeatable read; EFAULT does not retire |
| COMPLETE_REAP 0x8a ACK | `flags=2`, sequence/cookie, no payload | Explicit unpin/retirement; repeat returns ESTALE. Quarantined records cannot be discarded |
| FAULT_INFO 0x8b | No payload/object/sequence/offset/length | Fault result/cookie and quarantine state, without resetting anything |

Flags READ and ACK are mutually exclusive. The envelope's epoch is checked on
every job operation. Buffer/catalog operations and bootstrap CAPS are U06's
separately checked control adapter; this job decoder does not invent them.

WAIT captures the completion revision while holding the mutex, unlocks, and
uses Linux `wait_event_interruptible_hrtimeout` with revision inequality as its
condition. A completion arriving before the wait is entered therefore cannot
be lost. The worker increments the revision before `wake_up_interruptible`.
This is software observation followed by wakeup, not a claim of hardware IRQ.
poll uses level-triggered readable/error state; reading a completion does not
consume readiness. Only ACK clears the record. Legacy poll remains non-readable.

Closing while active or while any accepted completion is unacknowledged retains
references and quarantines; it never guesses DMA has stopped. Clean close after
all ACKs releases that fd's program-load references and objects. No implicit
reset, retry, PL operation or clearing of quarantine is provided.

## Verification bounds

The Ubuntu tests exercise the real common queue and decoder with an injected
OS/MMIO boundary, including 100 real pthread close/worker races, timeout/signal
delivery, copy faults, stale epoch and repeated readiness. They do not execute
kernel wait queues, actual device epoll, DMA or a GPU. The separate AArch64
Linux-5.4 module build checks compile/link and symbols, not runtime behavior.
The pre-binding module is not a deployment artifact and lacks final U23
application/provider binding. Every offline receipt has `gpu_raw_cycles=null`;
the injected raw counter 37 is a protocol fixture, never a measured GPU result.


## S8 joint user-space logical and device submission identities

The sole ComputeV2Backend retains the public request's logical cookie independently
from its strictly increasing device/wire cookie. Dispatch can reorder prepared
requests; it must not weaken the kernel's monotonic replay rejection. Each original
256-byte completion remains unchanged, while a separate receipt binds logical
cookie, submitted wire cookie, sequence and device epoch. P8 and NN/IMAGE adapters
validate this binding before trusting completion or ACK. Only the declared INT16
private ARGS cookie field is patched in a staging copy; caller DATA, arithmetic,
program bytes and the public ioctl ABI are unchanged.

Retired logical identities remain tombstoned for the backend/fd lifetime, including
a return-copy fault. The interval ledger reserves at most 1,048,576 pairs (16 MiB
of pair storage on the supported hosts), merges adjacent logical IDs and never
evicts a retired identity on ACK. Capacity exhaustion returns ENOSPC before the
SUBMIT ioctl; UINT64_MAX wire exhaustion returns EOVERFLOW without wrapping. An
attempt that reaches the ioctl boundary consumes its logical ID conservatively,
even if the ioctl returns a definitive error; callers must not reuse that ID.
Validation/upload failures before that boundary do not consume the identity.
A repeated live identity returns EALREADY and a retired identity returns ESTALE.
No automatic retry, hardware reset, context switch or GPU parallelism is added.

Verification is separate: host C driver/oracle/fake-KMS tests exercise actual
identity and lifecycle paths but do not execute GPU instructions. Host/offline
GPU raw cycles remain null; matching target compilation and field validation
require their own receipts.
