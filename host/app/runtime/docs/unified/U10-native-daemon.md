# Native service shell and explicit startup

`tools/vxgpud.cpp` links the real DeviceOwner, native ComputeExecutionPort or
LegacyExecutionPort, Runtime, ServiceControl, RpcServer and EvidenceJournal.
There is one execution pump and one device fd; the four-worker CPU pool remains
inside Runtime's compute-v2 mode. Legacy mode has no unused additional pool.
The production binary does not link the C hardware fixture or expose any test
transport, fixture, scope, raw descriptor, mode switch, reset or PL option.

No command starts implicitly. `vxgpud --check-config CONFIG` checks only a
same-uid regular one-link 0600/0400 JSON file, and opens no device or directories.
`vxgpud --serve CONFIG` is the explicit native entry. Configuration version1
contains private_dir, evidence_dir, device_path, backend_mode, legacy_profile,
hardware_contract, bit, xsa, compatible, device_generation and provider_flags.
There are no wire fields for these. Duplicate/unknown fields and auto mode fail.
The kernel's fresh `/proc/sys/kernel/random/boot_id` supplies the RPC epoch.
Physical verification cannot be asserted through JSON. Declared identity is
compared with actual CAPS or fixed-profile GET_INFO; neither implies a separate
physical bitstream measurement. There is no mode fallback or SELECT_PROFILE.

Startup saves synchronized content-addressed evidence with the complete identity,
config-byte SHA, boot ID, mode, admitted application names and null GPU cycles,
before opening RPC admission or launching the pump. Successful control admission
wakes the execution thread independently of socket delivery. Disconnect alone
does not submit/cancel; replaying an admitted key does not prepare or execute
again. Raw completion evidence precedes userspace ACK, and result publication
follows retirement and durable state/result evidence.

The first execution error stops further factory admission and execution. EAGAIN
is idle only while Runtime remains READY; an application error after ACK is not
idle. Query/control remains available and original Runtime errors take priority
over any subsequent pump error. Signal shutdown stops admission, waits for the
bounded current operation, joins the pump, then asks Runtime to retire. Unknown
state remains anchored through allocation-free process-exit retention, without
guessed release, retry, reset or PL reconfiguration.

U10's `installed_applications()` registry is deliberately empty until U11-U18
install the actual native operators/models/image/graphics bindings. Inventory
works but this does not claim a runnable NN or graphics application. The complete
25-task acceptance must not pass an empty registry, a host fixture, or this shell
as application/driver/board/image completion. `StartupContext` includes the fixed
legacy profile so future factories do not guess between old NN profiles 4/6.

The host daemon test exercises real Unix framing, credentials, Runtime, native
port and C control/retirement core, with an explicit C hardware transport fixture.
The separate legacy inventory test permits only GET_INFO/profile6, no v2 command
or idle common pool. Production CLI tests cover config-only/no-device, strict
schema, explicit modes, permissions/links, no fallback, and absence of bridge
symbols. None executes real kernel ioctls against `/dev/vortex0`, a neural graph,
DP or a physical GPU. Offline raw cycles remain null.


The first final sanitizer run (127) was not accepted: LeakSanitizer found
161 leaked bytes after refusing the second long error-string allocation. An
independent reduction without any controller, runtime or device reproduced this
for a plain aggregate pair under the actual GCC7/O2/no-elide host flags. Named
RAII assignment and explicit constructor parameters were clean (129). Error now
uses the latter, preserving its fields and message bytes and the kernel ABI.
The original 256-position control allocation test passed ASan/UBSan at130 with
186 refusal responses and7 accepted-before-reply-allocation-failure cases.
This is focused validation, not full sanitizer or target/board acceptance.
The diagnostic probe still preserves aggregate failures; the regression target
runs only the explicit constructor and actual Result<Error> repaired paths and
requires two genuinely injected allocation exceptions in each mode.

Faulted admission must not disable idempotent query semantics: an existing
identity with the identical registered payload returns its original status
without invoking the factory or GPU. A changed payload still conflicts and a
new identity is refused before prepare. The internal control overload carries
the pump admission gate; the original control API remains, with Runtime READY
checked before a new factory. No wire mode or recovery/execute command is added.
