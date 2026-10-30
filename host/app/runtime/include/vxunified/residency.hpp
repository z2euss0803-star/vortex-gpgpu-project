#pragma once
#include <vxunified/catalog.hpp>
#include <vxunified/backend.hpp>
#include <vxunified/work.hpp>
namespace vxunified {
// One request owns program+DATA residency. Queued work carries no handles.
// It must retire all device backing before returning this lease. The cached
// program can then be reused or evicted; hardware still checks pins/ranges.
struct ResidencyLease {RequestKey request;ProgramRef program;uint64_t serial=0;};
struct ResidencyStats {bool owned=false,cached=false,faulted=false,closed=false;uint64_t loads=0,evictions=0;std::optional<vxnn::Error> first_error;};
class ProgramResidency {
 struct Impl;std::unique_ptr<Impl>impl_;explicit ProgramResidency(std::unique_ptr<Impl>);
public:
 ~ProgramResidency();
 static Result<std::shared_ptr<ProgramResidency>> create(std::shared_ptr<const InstalledCatalog>,std::shared_ptr<UnifiedBackend>);
 // Pure metadata query, safe for scheduler pre-activation. No new I/O.
 Result<bool> ready(const RequestKey&,const ProgramKey&)const;
 Result<ResidencyLease> acquire(const RequestKey&,const ProgramKey&);
 // An already-owned request can plan a shader transition without evicting DATA.
 // Actual replacement needs its exact last ACK outcome+original completion;
 // the kernel still independently refuses active/pinned program release.
 Result<bool> ready_switch(const ResidencyLease&,const ProgramKey&)const;
 Result<ResidencyLease> switch_after_ack(const ResidencyLease&,const ProgramKey&,const WorkOutcome&,const vxl_v2_completion&);
 Result<void> release(const ResidencyLease&,bool device_backing_retired);
 const BackendIdentity& identity()const;
 std::shared_ptr<const InstalledCatalog>catalog()const;
 std::shared_ptr<UnifiedBackend> borrowed_backend()const;
 Result<void> shutdown();ResidencyStats stats()const;
};
}
