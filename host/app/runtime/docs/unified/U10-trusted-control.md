# Trusted control integration (U10 component, not daemon/application acceptance)

`ServiceControl` connects the already strict `RpcRequest` transport contract to
the real `Runtime`. Only an administrator-built, immutable C++ registry can supply
`TrustedApplication` factories, model/plan identities and registered input IDs,
hashes and byte lengths. The wire does not contain a callback, path to code,
device fd, raw descriptor, address, backend mode or PL command.

Startup compares the registry's complete hardware/bit/XSA/compatible/provider/
generation and physical-verification classification with Runtime's cached probe.
This getter does not issue another ioctl. A declared startup identity is still
declared: this check does not turn manifest hashes into physical measurement.
S8 admits NN/image entries and P8/W4 admits graphics entries; foreign entries
fail startup. Registry and per-entry input lists are each bounded at 64.

Factories may lower admitted software work but must not open the device, submit
work or execute it. They must validate their own scalar parameter schema. Their
returned key, app, model, plan, input SHA and byte count are checked before Runtime
admission. `handle` never runs the execution pump. Runtime continues to own all
pending/CPU/model accounting and resource leases.

The control layer reserves one of 256 identity records before calling a factory.
Canonical parameter JSON, entry ID and input ID bind idempotence. An accepted
request whose response allocation fails remains queryable without another
factory call or execution. Failed admission removes the reservation. Reaped
client results keep a tombstone: the same request ID cannot silently run again;
changed payload returns EEXIST, replay of a reaped identity returns EALREADY.
The 256-record bound is deliberate; it currently requires a clean service
restart after safe retirement to begin a new identity epoch, rather than unsafe
automatic ID recycling. Runtime itself retains its existing bounded index.

Result responses preserve the old result JSON as an exact escaped string,
historical numeric classification, artifact hashes/layout/shape and separate
execution/display states. Offline GPU cycles remain null. Large results remain
subject to the existing 64KiB RPC response limit; a refusal does not reap them.
Display and export require separately registered bounded consumer hooks, which
must establish their own leases/retirement evidence. Their absence returns
EOPNOTSUPP, never a synthetic display/export success. Actual hooks are U17/U18.

`test_service_control` and `test_control_alloc` use real owner, Runtime,
Scheduler and ResultStore code but an explicit host execution port and trusted
host factory. These are not native C graph execution, actual GPU/DP tests or a
production `vxgpud`. The production service entry point, native application
factory registration, execution-pump wiring and final full U10 validation remain
open. Existing socket/CLI tests continue to use their separate transport fixture.
