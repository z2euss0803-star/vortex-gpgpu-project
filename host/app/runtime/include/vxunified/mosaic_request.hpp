#pragma once
#include <vxnn/request_input.hpp>
#include <vxunified/service.hpp>
#include <vxunified/residency.hpp>
#include <vxnn/model_mosaic.hpp>
#include <vxnn/arena_guard.hpp>
#include <vxunified/mosaic_profile.hpp>
namespace vxunified {
struct MosaicJobTrace {
 uint64_t index=0;vxnn::NodeId node=0;uint32_t first=0,count=0;int error=0;bool phase_complete=false;
 uint64_t begin_ns=0,end_ns=0,program_ns=0,allocate_ns=0,upload_ns=0,args_ns=0,guard_ns=0;
 vxnn::EvidenceScope scope=vxnn::EvidenceScope::HOST_PROTOCOL_TEST;
 std::optional<uint64_t>gpu_raw_cycles;std::optional<vxl_v2_completion>linux_detail;
 std::optional<VxnnMosaicArgs>stage_args;std::optional<MosaicProfileRecords>stage_records;uint64_t profile_prepare_ns=0,profile_read_ns=0;
};
struct MosaicPhaseReceipt {vxnn::NodeId node=0;uint64_t completed_jobs=0,begin_ns=0,end_ns=0;std::optional<uint64_t>gpu_raw_cycles;};
struct MosaicRequestReceipt {
 uint64_t completed_jobs=0,completed_phases=0,download_ns=0,publish_ns=0,release_ns=0;
 bool materialized=false,retired=false,published=false;
 std::optional<vxnn::Error>first_error,evidence_error;std::optional<vxnn::ArenaGuardFault>guard_fault;
 std::vector<MosaicPhaseReceipt>phases;
};
// Fixed512-byte private evidence record. Run manifest anchors source/model and
// clocks; each record retains the original256B Linux completion verbatim.
Result<std::array<uint8_t,512>> encode_mosaic_job_trace(const MosaicJobTrace&);
Result<std::vector<uint8_t>> encode_mosaic_extended_trace(const MosaicJobTrace&);
using MosaicTraceSink=std::function<Result<void>(const RequestKey&,const MosaicJobTrace&)>;
using MosaicOutputSink=std::function<Result<PublishedResult>(const WorkRequest&,vxnn::Bytes,const MosaicRequestReceipt&)>;
// Bounded lazy source: at most77 prefix sums, one current job, no eager million-job
// allocation. Destruction never retries device I/O or releases unknown pins.
class MosaicRequest final:public WorkSource,public std::enable_shared_from_this<MosaicRequest> {
 struct Impl;std::unique_ptr<Impl>impl_;explicit MosaicRequest(std::unique_ptr<Impl>);
public:
 ~MosaicRequest();
 static Result<std::shared_ptr<MosaicRequest>>prepare(vxnn::ModelPackage,const vxnn::MosaicFamily&,const DeviceCaps&,const BackendIdentity&,RequestKey,std::shared_ptr<ProgramResidency>,vxnn::EvidenceScope,uint32_t cookie_namespace,uint64_t budget_ns,MosaicTraceSink,MosaicOutputSink,std::shared_ptr<const vxnn::RequestInput> input={});
 uint64_t size()const override;Digest identity()const override;Result<WorkItem>get(uint64_t)const override;Result<bool>ready(uint64_t)const override;
 WorkRequest request()const;ExecutionHooks handlers();const vxnn::Plan&plan()const;const MosaicRequestReceipt&receipt()const;
};
}
