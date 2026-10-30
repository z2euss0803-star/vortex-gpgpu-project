#pragma once
#include <vxunified/control.hpp>
namespace vxunified {
class EvidenceJournal;
struct DaemonConfig {
 ServiceConfig service;BackendIdentity identity;std::string boot_id,evidence_dir;
 Digest config_sha{};uint32_t legacy_profile=0;
 bool fast_runtime=false;
 vxnn::EvidenceScope scope=vxnn::EvidenceScope::REAL_BOARD;
};
struct ApplicationSet {
 std::vector<TrustedApplication> entries;ExecutionHooks compute;LegacyExecutionHooks legacy;
 ResultControl display,export_result;
 std::optional<Digest>installation_sha;
 // Trusted native ingress/display/program owners. Shutdown must stop ingress
 // before joining execution, retire display before Runtime, then release the
 // shared program cache. A failed stage is retained, never retried implicitly.
 std::function<Result<void>()> activate,quiesce,retire_display,release_resources;
 // Only trusted compiled ingress may install these hooks. Control I/O is
 // bounded; metadata/completion shares the existing sole execution worker.
 std::function<Result<void>(uint32_t)>poll_native;
 std::function<Result<bool>()>prepare_native;
 // Retire already completed native backing after mode advance and before any
 // preparation/dispatch. This hook may not create or submit application work.
 std::function<Result<void>()>settle_native;
};
struct StartupContext {
 int borrowed_fd=-1;BackendMode mode=BackendMode::COMPUTE_V2;BackendIdentity identity;uint32_t legacy_profile=0;
 std::shared_ptr<UnifiedBackend> compute_backend;
 std::string private_dir,boot_id;
 bool fast_runtime=false;
 // Borrow the common service roots. Native bindings must not capture a new
 // device owner, private per-app result store or strong Runtime cycle.
 std::weak_ptr<Runtime> runtime;std::weak_ptr<ResultStore>results;
 std::weak_ptr<EvidenceJournal>journal;
 vxnn::EvidenceScope scope=vxnn::EvidenceScope::REAL_BOARD;
 std::function<Result<void>()> notify_work;
};
// Trusted compiled registration only. It may prepare resources/programs but must
// never submit/START jobs during startup. Native app bindings follow U11-U18.
using ApplicationBuilder=std::function<Result<ApplicationSet>(const StartupContext&)>;
class Daemon {
 struct State;std::shared_ptr<State>state_;
 explicit Daemon(std::shared_ptr<State>);
public:
 ~Daemon();Daemon(const Daemon&)=delete;Daemon&operator=(const Daemon&)=delete;
 static Result<std::shared_ptr<Daemon>> start(const DaemonConfig&,ApplicationBuilder,DeviceOpen={},DeviceClose={},TransportFactory={});
 Result<void> serve_one(uint32_t timeout_ms=500);
 Result<void> shutdown();
 Result<vxnn::Error> first_error()const;
};
}
