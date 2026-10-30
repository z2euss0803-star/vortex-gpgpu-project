# U06 component checkpoint (not whole-task acceptance)

## Frozen provider admission

S8 and P8 use the exact hardware-contract, bit, XSA and compatible anchors in
`contracts/unified/providers.json`. Matching numeric profile/program IDs alone
does not admit a program. S8 admits NN/IMAGE namespaces; P8 admits namespace2,
profile5/program9..12. These are boot-manifest declarations and never attest to
the physical PL independently. Startup provider selection and full per-entry
admission must finish before enabling the new fd path.

The new v2 CAPS `feature_mask` advertises bit0 NATIVE_INT16 or bit1 P8_GRAPHICS;
this implementation supports exactly one, not both. This completes the draft
CAPS producer's previous zero placeholder. Existing request layouts, command
numbers, old 176-byte ABI and all old program identities stay unchanged.

## PS ownership and actual address domains

| PS offset | Consumer | Public allocator |
| --- | --- | --- |
| 0x2000..0x3000 | trusted CRT declaration | excluded |
| 0x30000..0x30040 | fence | excluded |
| 0xfe000..0xff000 | v2 DMA bounce | excluded |
| 0xff000..0x100000 | trusted stack declaration | excluded |

The legacy hardware adapter's bounce at 0xff000 is unchanged and is never used
as the compute-v2 adapter. Native memory operations map trusted private CRT/stack
through the actual coherent PS allocation with DMA base 0x10000000. They reject
any overlap with the v2 bounce region. Public allocations skip all reservations.
The provider must retain one mapped PS pool for the device lifetime; neither a
malloc pointer nor a GPU32 value is substituted for its DMA address.

PL transfer uses the shared bounded CDMA mechanism: IOC is required after an
actual transfer, while a cold idle status need not contain an old IOC. Tails use
16-byte read/modify/write, with at most4096 bytes through the reserved bounce.
This is software implementation tested with an injected CDMA register boundary,
not measured hardware DMA. No old NN9 fastwait condition was broadened to P8.

TEX preparation requires idle GPU, CDMA and TEX, validated pinned DATA/READONLY
job references, and bounded RGBA texture geometry. It writes and reads back the
whole descriptor as one locked state transition. Actual per-program selection
of this state remains part of the unfinished provider execution integration.
It must preserve P8's frozen kernels and visuals, not enable TEX speculatively.

## Typed control payloads

The envelope remains128 bytes. CREATE/INFO payloads are64 bytes; COPY is128 bytes;
program load/info is64 bytes. Static C assertions cover their sizes. Public
CREATE accepts DATA or ARGS; machine code and rodata come only from the fixed
trusted catalog paths. INFO returns typed references, never CPU pointers or raw
physical/GPU addresses. CPU addresses only appear in COPY's separate user_ptr.
CPU/GPU/DMA/TEX/DP pins are checked before transfer or userspace payload reads.
Copies are bounded to64 MiB per ioctl; the backend segments larger copies and
stops at the first error. A failure may leave a partial user-owned upload and is
not silently retried. Overlapping device copies are rejected.

## C++ adapter semantics

`ComputeV2Backend` implements the unchanged `IGpuBackend` virtual interface.
Its transport borrows the service's unique owner fd, with no automatic reset,
close, reconfiguration or resubmission. The new compute implementation's
`program_load(Digest)` consumes a catalog entry ID; the inherited
`ProgramRef.program_sha` member retains that entry ID, not a guessed code hash.

JobDesc.deadline_ns is absolute Linux CLOCK_MONOTONIC time. After parameter
transfer, the backend derives the remaining (at most60s) execution budget. It
does not compare A/B wall clocks. WAIT timeout is a caller wait budget only.

Because the old allocate interface has no purpose argument, submit creates a
private typed ARGS staging object from the caller's ordinary parameter buffer.
The core then reconstructs addresses and snapshots those bytes. Staging is
retained until explicit ACK, including uncertain SUBMIT EFAULT. Cookie lookup
can retrieve the original sequence without START again. Duplicate program-load
references have a user-side count matching the core. A known ACK-return EFAULT
can be explicitly resolved by another ACK; only a successful kernel-checked
buffer release frees staging. Subtensor views cannot release their base object.
The additional staging/transfer cost is to be measured in U19; no speedup is
claimed here. `detail()` retains the full raw completion fields and Linux
phase timings, including an observed but invalid counter; the public optional
raw_cycles is populated only when the completion's validity bit is true.

## Remaining U06 work

This is a component checkpoint. Actual startup DT/manifest/catalog attachment,
matching execution prepare/START/snapshot/guard/retirement/ACK callbacks and
provider-wide failure tests are still required. Final U22/U23 application/ELF
binding, service link and matching image audit also remain. A pre-binding module
or C++ object is not deployable acceptance. All offline receipts retain
`gpu_raw_cycles=null`; the fixture counter37 is not a physical measurement.
No board, PL or SD operation is authorized by this checkpoint.
