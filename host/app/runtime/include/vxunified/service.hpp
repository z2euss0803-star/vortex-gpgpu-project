#pragma once
#include <vxunified/work.hpp>
#include <vxunified/mode.hpp>
#include <vxunified/scheduler.hpp>
#include <vxunified/backend.hpp>
#include <uapi/vx_linux.h>
#include <functional>
#include <memory>
namespace vxunified {
// Trusted startup configuration; never accepted from the RPC peer.
struct ServiceConfig {
 std::string private_dir,device_path="/dev/vortex0";
 BackendMode mode=BackendMode::COMPUTE_V2;
};
using DeviceOpen=std::function<Result<int>(const std::string&)>;
using DeviceClose=std::function<void(int)>;
class DeviceOwner {
 struct State;std::unique_ptr<State> state_;
 explicit DeviceOwner(std::unique_ptr<State>);
public:
 ~DeviceOwner();DeviceOwner(const DeviceOwner&)=delete;DeviceOwner&operator=(const DeviceOwner&)=delete;
 static Result<std::shared_ptr<DeviceOwner>> acquire(const ServiceConfig&,DeviceOpen={},DeviceClose={});
 int fd()const;BackendMode mode()const;
};
struct ExecutionBinding {
 AppKind app=AppKind::NN;
 Digest hardware{},bit{},xsa{},compatible{},model{},plan{},input{},source{};
 uint64_t device_generation=0,input_bytes=0;
 uint32_t provider_flags=0;
 bool operator==(const ExecutionBinding&o)const{return app==o.app&&hardware==o.hardware&&bit==o.bit&&xsa==o.xsa&&compatible==o.compatible&&model==o.model&&plan==o.plan&&input==o.input&&source==o.source&&device_generation==o.device_generation&&input_bytes==o.input_bytes&&provider_flags==o.provider_flags;}
};
// This is an execution receipt, not a combined execution/numeric/display PASS.
struct PublishedResult {
 std::optional<ExecutionBinding> binding;
 ResultLease lease;
 vxnn::NumericClass numeric=vxnn::NumericClass::NOT_EVALUATED;
 std::string raw_legacy_json;
 bool execution_complete=false,displayed=false;
 std::optional<uint64_t> gpu_raw_cycles;
};
// Pure shared metadata validation, before adapter backing retirement.
Result<void> validate_published_result(const PublishedResult&);
struct ConsumerLease {uint64_t token=0;RequestKey producer;Consumer consumer=Consumer::CPU;};
struct ResultStoreStats {size_t records=0,consumer_leases=0,client_holds=0,pinned_retire_failures=0,staged_records=0;};
// Callbacks belong to admitted adapters. persist must durably retain the exact
// receipt; retire must establish resource retirement before releasing backing.
// They must be bounded/non-reentrant. In particular a failed retire retains it.
using PersistResult=std::function<Result<void>(const PublishedResult&)>;
using RetireResult=std::function<Result<void>(const ResultLease&)>;
class ResultStore {
 struct State;std::shared_ptr<State> state_;
public:
 ResultStore(PersistResult,RetireResult);
 ~ResultStore();ResultStore(const ResultStore&)=delete;ResultStore&operator=(const ResultStore&)=delete;
 Result<void> publish(const PublishedResult&);
 // Internal checked-output staging for native presentation. The separate
 // callback records a non-public stage, never a completed client result.
 Result<void> stage_display(const PublishedResult&,PersistResult stage_journal);
 Result<PublishedResult> take_result(RequestKey)const;
 Result<ConsumerLease> retain_result(RequestKey,Consumer);
 Result<PublishedResult> display_source(const ConsumerLease&)const;
 Result<void> bind_display_cookie(const ConsumerLease&,uint64_t logical_cookie);
 Result<void> release_result(const ConsumerLease&,bool confirmed_retired);
 Result<void> mark_displayed(const ConsumerLease&,const DisplayReceipt&);
 Result<void> reap_result(RequestKey);
 ResultStoreStats stats()const;
};
struct RpcRequest {
 std::string command,entry_id,input_id,destination_id,parameters;
 RequestKey request;uint32_t artifact=0;
};
// Callback performs admission/control only; execution runs outside this socket
// thread. Device paths, programs, raw descriptors and mode are never wire data.
using RpcHandler=std::function<Result<std::string>(const RpcRequest&)>;
class RpcServer {
 struct State;std::unique_ptr<State> state_;
 explicit RpcServer(std::unique_ptr<State>);
public:
 ~RpcServer();RpcServer(const RpcServer&)=delete;RpcServer&operator=(const RpcServer&)=delete;
 static Result<std::shared_ptr<RpcServer>> start(const std::string&private_dir,const std::string&boot_id,RpcHandler);
 Result<void> serve_one(uint32_t timeout_ms=1000);
 // Internal common control-loop multiplexing: idle listener succeeds without
 // accepting a peer; an actual peer keeps all original framed I/O deadlines.
 Result<void>serve_if_ready(uint32_t peer_timeout_ms=1000);
};
enum class DeliveryStage {RAW_RETIREMENT,REQUEST_STATE,EXECUTION_ERROR,ACK_RETIREMENT};
enum class RuntimePhase {READY,DELIVERY_BLOCKED,FAULTED,CLOSING};
struct ExecutionReceipt {
 ExecutionBinding binding;
 WorkOutcome outcome;
 std::optional<PublishedResult> output;
 std::optional<vxl_v2_completion> linux_detail;
 std::optional<vxl_request> legacy_detail;
 std::optional<vxnn::Error> execution_error,evidence_error,recovery_error;
 uint64_t recovery_attempts=0,recovery_begin_ns=0,recovery_end_ns=0;
 RequestStatus delivered_status=RequestStatus::RUNNING;
 uint64_t dispatch_begin_ns=0,dispatch_end_ns=0,execute_begin_ns=0,execute_end_ns=0;
 uint64_t retire_begin_ns=0,retire_end_ns=0;
 uint64_t legacy_call_begin_ns=0,legacy_call_end_ns=0;
 // Admission/submission identity is retained even if WAIT returns no completion.
 uint64_t submitted_cookie=0,submitted_sequence=0,program_handle=0,program_generation=0,device_generation=0;
 // Original ioctl cookie, explicitly separate from prepared logical work.
 uint64_t submitted_wire_cookie=0;
 Digest program_entry_id{};
 uint64_t submit_begin_ns=0,submit_end_ns=0,wait_begin_ns=0,wait_end_ns=0,read_begin_ns=0,read_end_ns=0;
 uint64_t prepare_begin_ns=0,prepare_end_ns=0;
 vxnn::EvidenceScope scope=vxnn::EvidenceScope::HOST_PROTOCOL_TEST;
 vxnn::ClockDomain clock_domain=vxnn::ClockDomain::HOST_MONOTONIC;
};
// Check the explicit logical -> wire -> sequence/epoch binding, while leaving
// linux_detail as the driver's original 256 bytes for persistence and audit.
inline bool matching_submission(const ExecutionReceipt&r,uint64_t logical){
 if(!logical||!r.outcome.completion||!r.linux_detail)return false;
 const auto&c=*r.outcome.completion;const auto&d=*r.linux_detail;
 return r.submitted_cookie==logical&&c.cookie==logical&&r.submitted_wire_cookie&&c.wire_cookie==r.submitted_wire_cookie&&d.cookie==c.wire_cookie&&r.submitted_sequence&&c.sequence==r.submitted_sequence&&d.sequence==c.sequence&&r.device_generation&&d.device_generation==r.device_generation;
}
class ExecutionPort {
public:
 virtual ~ExecutionPort()=default;
 // Must query only; the sole fd is borrowed, never opened or closed here.
 virtual Result<BackendIdentity> probe(int borrowed_fd,BackendMode)=0;
 // Startup-only borrowed alias. Non-native/legacy ports have none. Runtime
 // snapshots it once before callbacks, without depending on a concrete port.
 virtual Result<std::shared_ptr<UnifiedBackend>> native_backend_snapshot(){return Result<std::shared_ptr<UnifiedBackend>>::success({});}
 // The receipt lives in Runtime before execution. Unknown retirement must
 // return an error and keep all backing; no implicit resubmit/reset is allowed.
 virtual Result<void> execute(int borrowed_fd,const WorkItem&,vxnn::CpuPool*,ExecutionReceipt&)=0;
 // Bounded observation of the ORIGINAL retained job on the SAME owner fd.
 // It may observe a completion, but must not submit, ACK, reset or release it.
 virtual Result<void> recover_completion(int,const WorkItem&,ExecutionReceipt&,uint64_t){return Result<void>::failure({EOPNOTSUPP,"PORT","no same-owner late completion observer"});}
 // Called only after the raw completion persistence callback succeeds. A
 // failure cannot be treated as proof that the device/resources have retired.
 virtual Result<void> retire_completed(int,const WorkItem&,ExecutionReceipt&){return Result<void>::failure({EOPNOTSUPP,"PORT","no completion retirement adapter"});}
 virtual Result<void> retire_request(int borrowed_fd,RequestKey)=0;
};
struct ExecutionHooks {
 std::function<Result<void>(const WorkItem&,UnifiedBackend&,vxnn::CpuPool*,ExecutionReceipt&)> non_gpu;
 std::function<Result<void>(const WorkItem&,UnifiedBackend&,ExecutionReceipt&)> finish_gpu;
 std::function<Result<void>(RequestKey,UnifiedBackend&)> retire_request;
 // Trusted owner hook, dispatched only after acquiring the single GPU slot.
 std::function<Result<GPUJobDesc>(const WorkItem&,UnifiedBackend&,ExecutionReceipt&)> prepare_gpu;
};
// Factory is an internal OS boundary for host tests. Production uses the sole
// owner's borrowed fd; it never accepts a transport or descriptor from RPC.
using TransportFactory=std::function<std::unique_ptr<IoctlTransport>(int)>;
class ComputeExecutionPort final:public ExecutionPort {
 struct Impl;std::unique_ptr<Impl> impl_;
public:
 ComputeExecutionPort(std::shared_ptr<DeviceOwner>,ExecutionHooks,TransportFactory={});
 ~ComputeExecutionPort() override;
 Result<BackendIdentity> probe(int,BackendMode)override;
 Result<std::shared_ptr<UnifiedBackend>> adapter_backend();
 Result<std::shared_ptr<UnifiedBackend>> native_backend_snapshot()override;
 Result<void> execute(int,const WorkItem&,vxnn::CpuPool*,ExecutionReceipt&)override;
 Result<void> recover_completion(int,const WorkItem&,ExecutionReceipt&,uint64_t)override;
 Result<void> retire_completed(int,const WorkItem&,ExecutionReceipt&)override;
 Result<void> retire_request(int,RequestKey)override;
};
struct LegacyExecutionHooks {
 std::function<Result<void>(int,const WorkItem&,vxnn::CpuPool*,ExecutionReceipt&)> run;
 std::function<Result<void>(int,RequestKey)> retire_request;
};
// Binding is a trusted startup manifest declaration, not independent physical
// measurement. GET_INFO additionally checks the actual fixed profile/epoch.
// Legacy NN profiles 4/6 do not acquire the v2 catalog or switch profiles.
class LegacyExecutionPort final:public ExecutionPort {
 struct Impl;std::unique_ptr<Impl> impl_;
public:
 LegacyExecutionPort(std::shared_ptr<DeviceOwner>,BackendIdentity declared,uint32_t fixed_profile,LegacyExecutionHooks,TransportFactory={});
 ~LegacyExecutionPort()override;
 Result<BackendIdentity>probe(int,BackendMode)override;
 Result<void>execute(int,const WorkItem&,vxnn::CpuPool*,ExecutionReceipt&)override;
 Result<void>retire_completed(int,const WorkItem&,ExecutionReceipt&)override;
 Result<void>retire_request(int,RequestKey)override;
};
using PersistWork=std::function<Result<void>(const ExecutionReceipt&,DeliveryStage)>;
struct RuntimeStats {
 RuntimePhase phase=RuntimePhase::FAULTED;SchedulerStats scheduler;
 uint64_t input_bytes=0,cpu_bytes=0;uint32_t resident_models=0,resident_leases=0,preparing_requests=0,resident_nn_models=0;
};
struct RequestDiagnostic {
 RequestStatus status=RequestStatus::QUEUED;
 std::optional<vxnn::Error>execution_error,evidence_error,recovery_error;
 bool device_pending=false,retirement_unknown=false,recovered=false,resources_retained=false;
 uint64_t submitted_sequence=0,submitted_cookie=0,submitted_wire_cookie=0,device_generation=0;
};
class PreparationLease {
 struct State;std::shared_ptr<State>state_;friend class Runtime;
 explicit PreparationLease(std::shared_ptr<State>);
public:
 ~PreparationLease();PreparationLease(const PreparationLease&)=delete;PreparationLease&operator=(const PreparationLease&)=delete;
};
// Trusted native session allocation reservation, not a remote token. Reserve
// before allocating immutable model/shadow memory; hold until that memory is
// destroyed. Active request accounting independently pins the same model.
class ResidentLease {
 struct State;std::shared_ptr<State>state_;friend class Runtime;
 explicit ResidentLease(std::shared_ptr<State>);
public:
 ~ResidentLease();ResidentLease(const ResidentLease&)=delete;ResidentLease&operator=(const ResidentLease&)=delete;
 Digest model()const;uint64_t bytes()const;AppKind app()const;
};
// Host buffers and retained RAM-backed output/evidence share the SAME CPU
// budget. They are not additional NN models or another device owner. Trusted
// factories must acquire before allocation and retain through actual lifetime.
class HostMemoryLease {
 struct State;std::shared_ptr<State>state_;friend class Runtime;friend class NativeFiles;friend class NativeRequestFiles;
 explicit HostMemoryLease(std::shared_ptr<State>);
 // File-store credits are admitted by Runtime BEFORE merging. These private
 // operations never grow the global budget or allocate another owner/table.
 Result<void>absorb(HostMemoryLease&);
 void release_bytes(uint64_t);
public:
 ~HostMemoryLease();HostMemoryLease(const HostMemoryLease&)=delete;HostMemoryLease&operator=(const HostMemoryLease&)=delete;
 uint64_t bytes()const;
};
class Runtime {
 struct State;std::shared_ptr<State> state_;
 explicit Runtime(std::shared_ptr<State>);
public:
 ~Runtime();Runtime(const Runtime&)=delete;Runtime&operator=(const Runtime&)=delete;
 static Result<std::shared_ptr<Runtime>> start(std::shared_ptr<DeviceOwner>,const BackendIdentity&,vxnn::EvidenceScope,std::shared_ptr<ExecutionPort>,PersistWork,PersistResult);
 Result<std::shared_ptr<ResidentLease>> reserve_resident(Digest model,uint64_t bytes);
 Result<std::shared_ptr<ResidentLease>> reserve_resident(Digest model,uint64_t bytes,AppKind);
 Result<std::shared_ptr<HostMemoryLease>>reserve_host_memory(uint64_t bytes);
 bool owns_host_memory(const std::shared_ptr<HostMemoryLease>&)const;
 // Trusted application factory/owner only. Staging is accepted solely from
 // the active final non-GPU execution callback and receives its real binding.
 Result<void> stage_display(PublishedResult,PersistResult stage_journal);
 std::shared_ptr<ResultStore> trusted_results()const;
 bool owns_resident(const std::shared_ptr<ResidentLease>&)const;
 bool owns_native_backend(int borrowed_fd,const UnifiedBackend*)const;
 vxnn::EvidenceScope evidence_scope()const;
 Result<std::shared_ptr<PreparationLease>>reserve_preparation(const RequestKey&,std::optional<AppKind> app={});
 Result<RequestKey> submit(const WorkRequest&,const std::shared_ptr<PreparationLease>&preparation={});
 Result<RequestStatus> status(RequestKey)const;
 Result<RequestDiagnostic> diagnostic(RequestKey)const;
 Result<void> cancel(RequestKey);
 // One execution pump, no per-request threads. RPC never calls execute.
 Result<void> run_one();
 Result<void> retry_delivery();
 // Trusted control only. Bounds a late WAIT; never repeats SUBMIT or ACK.
 Result<void> recover_pending(uint64_t timeout_ns=100000000ULL);
 Result<PublishedResult> take_result(RequestKey)const;
 Result<ConsumerLease> retain_result(RequestKey,Consumer);
 Result<void> release_result(const ConsumerLease&,bool confirmed_retired);
 Result<void> reap_result(RequestKey);
 Result<void> shutdown();
 // Trusted close only, after ingress/display retirement and shutdown. Drops
 // client holds, never consumer/staged/fault holds or uncertain backing.
 Result<void> drain_shutdown_results();
 Result<vxnn::Error> first_error()const;
 // Read-only cached startup probe; no new device open, ioctl or mode change.
 BackendIdentity identity()const;
 RuntimeStats stats()const;
 // Joint platform only. This requests a handoff; it never preempts a frame.
 Result<ModeSnapshot>request_mode(AppKind);
 Result<bool>advance_mode();
 bool mode_allows(AppKind)const;
 ModeSnapshot mode_snapshot()const;
};
}
