#pragma once
#define VXUNIFIED_HUNET_PAGE_CORE_V1 1
#include <vxunified/hunet_page_identity.hpp>
#include <vxnn/request_input.hpp>
#include <vxunified/service.hpp>
#include <vxunified/residency.hpp>
#include <vxnn/model_hunet.hpp>
#include <vxnn/arena_guard.hpp>
#include <vxnn/ops_hunet.hpp>
namespace vxunified {
struct HunetNodeTrace {
 vxnn::NodeId node=0;bool cpu=false,complete=false;int error=0;
 uint32_t word_index=0;uint64_t global_work_index=0;
 uint64_t begin_ns=0,end_ns=0,program_ns=0,allocate_ns=0,seed_upload_ns=0,args_ns=0,guard_ns=0,download_ns=0,publish_ns=0,release_ns=0;
 std::optional<uint64_t>gpu_raw_cycles;std::optional<vxl_v2_completion>linux_detail;
 std::optional<vxnn::HunetGeluReceipt>gelu;
 std::optional<vxnn::HunetStatsReceipt>gn_stats;
 std::optional<std::array<uint32_t,2>>gn_gpu_bits;
};
struct HunetRequestReceipt {
 uint64_t completed=0;bool materialized=false,retired=false,published=false;
 std::optional<vxnn::Error>first_error,evidence_error;
 std::optional<vxnn::ArenaGuardFault>guard_fault;
 std::array<HunetNodeTrace,116>nodes{}; // Current word only; trace persists every row.
 uint32_t words_planned=1,words_completed=0;
 uint64_t constant_uploads=0,constant_upload_bytes=0,input_uploads=0,input_upload_bytes=0,data_allocations=0,args_allocations=0;
 std::optional<uint64_t>gpu_raw_cycles;
 Digest page_logits_sha{}; // Concatenated raw words, set before page final sink.
};
struct HunetWordIdentity {
 uint32_t word_index=0;std::string input_id;
 Digest source_sha{},input_sha{},preprocessing_id{},plan{};
};
// A durable trace/container reference, deliberately not a fake ResultArtifact.
// Trusted sink must copy all bytes and fdatasync before returning this receipt.
struct HunetWordCommit {
 RequestKey producer;HunetWordIdentity word;Digest logits_sha{};
 uint64_t logits_bytes=32420;std::string durable_record;
};
using HunetInputLoader=std::function<Result<std::shared_ptr<const vxnn::RequestInput>>(uint32_t)>;
using HunetWordSink=std::function<Result<HunetWordCommit>(const WorkRequest&,const HunetWordIdentity&,vxnn::Bytes,const HunetRequestReceipt&)>;
using HunetPageOutputSink=std::function<Result<PublishedResult>(const WorkRequest&,const std::vector<HunetWordCommit>&,const HunetRequestReceipt&)>;
// Trusted owner supplies these callbacks. Publication must durably copy/hash
// all output bytes before returning; a result lease then refers to files/host
// backing, never to a DATA arena that may be evicted. U20 installs real sinks.
using HunetTraceSink=std::function<Result<void>(const RequestKey&,const HunetNodeTrace&)>;
using HunetOutputSink=std::function<Result<PublishedResult>(const WorkRequest&,vxnn::Bytes,const HunetRequestReceipt&)>;
// One request, shared device owner/residency and shared runtime CPU pool.
// prepare/get/ready are metadata only; handlers run only at owner dispatch.
// Call receipt/handlers on the one execution thread. No destructor device I/O.
class HunetRequest final:public WorkSource,public std::enable_shared_from_this<HunetRequest> {
 struct Impl;std::unique_ptr<Impl>impl_;explicit HunetRequest(std::unique_ptr<Impl>);
public:
 ~HunetRequest();
 static Result<std::shared_ptr<HunetRequest>>prepare(vxnn::ModelPackage,const vxnn::HunetFamily&,const DeviceCaps&,const BackendIdentity&,RequestKey,std::shared_ptr<ProgramResidency>,vxnn::EvidenceScope,uint32_t cookie_namespace,uint64_t job_budget_ns,HunetTraceSink,HunetOutputSink,std::shared_ptr<const vxnn::RequestInput> input={});
 // Metadata content must be null. Loader runs only in an execution hook at a
 // safe word boundary; it cannot reenter Runtime or perform device I/O.
 static Result<std::shared_ptr<HunetRequest>>prepare_page(vxnn::ModelPackage,const vxnn::HunetFamily&,const DeviceCaps&,const BackendIdentity&,RequestKey,std::shared_ptr<ProgramResidency>,vxnn::EvidenceScope,uint32_t cookie_namespace,uint64_t job_budget_ns,std::vector<vxnn::RequestInput> words,HunetInputLoader,HunetTraceSink,HunetWordSink,HunetPageOutputSink);
 // Current input-bound word, or base before first load; request().plan=pageID.
 const vxnn::Plan&base_plan()const;
 uint64_t size()const override;Digest identity()const override;
 Result<WorkItem>get(uint64_t)const override;Result<bool>ready(uint64_t)const override;
 WorkRequest request()const;const vxnn::Plan&plan()const;const HunetRequestReceipt&receipt()const;
 ExecutionHooks hooks();
};
}
