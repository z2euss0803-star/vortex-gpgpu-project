#pragma once
#include <vxunified/types.hpp>
#include <vxnn/graph.hpp>
#include <variant>
#include <memory>
namespace vxnn {struct CpuCancel;}
namespace vxunified {
class WorkSource;
enum class WorkKind {GPU,CPU,TRANSFER,PRESENT,LEGACY_EXCLUSIVE};
enum class RequestStatus {CREATED,VALIDATED,QUEUED,RUNNING,CANCEL_PENDING,COMPLETED,FAILED,CANCELLED};
struct CpuTaskRef {std::string registered_name;vxnn::NodeId node=0;};
// Logical admitted GPU work. No invented/resident program handle at queue time.
struct DeferredGpuRef {std::string registered_name;vxnn::NodeId node=0;uint64_t device_generation=0,cookie=0;};
using GPUJobDesc=JobDesc;
struct WorkItem {RequestKey request;AppKind app=AppKind::NN;vxnn::NodeId node=0;WorkKind kind=WorkKind::GPU;std::vector<uint64_t> dependencies;std::variant<GPUJobDesc,CpuTaskRef,DeferredGpuRef> operation;uint64_t queue_deadline_ns=0,work_index=0,pending_token=0;std::shared_ptr<vxnn::CpuCancel> cancel_control;};
// Logical identity survives preparation and retirement without fake handles.
inline uint64_t gpu_work_epoch(const WorkItem&w){if(auto*p=std::get_if<GPUJobDesc>(&w.operation))return p->device_generation;if(auto*p=std::get_if<DeferredGpuRef>(&w.operation))return p->device_generation;return 0;}
inline uint64_t gpu_work_cookie(const WorkItem&w){if(auto*p=std::get_if<GPUJobDesc>(&w.operation))return p->cookie;if(auto*p=std::get_if<DeferredGpuRef>(&w.operation))return p->cookie;return 0;}
// An opaque v1 aggregate has no v2 cookie/sequence or userspace v2 ACK.
// The trusted compatibility adapter binds its original result and GET_INFO.
struct LegacyRetirementProof {uint64_t device_generation=0;uint32_t profile_id=0;bool aggregate_complete=false,quiescent=false;Digest result_sha{},info_sha{};};
struct WorkOutcome {RequestKey request;vxnn::NodeId node=0;RequestStatus status=RequestStatus::FAILED;int result=0;std::optional<Completion> completion;std::optional<LegacyRetirementProof> legacy;uint64_t work_index=0;bool acknowledged=false,retired=false;};
struct ResultArtifact {std::string path,layout;Digest sha{};vxnn::Shape shape;uint64_t bytes=0;};
struct ResultLease {RequestKey producer;AppKind app=AppKind::NN;vxnn::EvidenceScope scope=vxnn::EvidenceScope::OFFLINE;Digest hardware{},model{},plan{};std::vector<ResultArtifact> artifacts;std::vector<uint64_t> lease_tokens;};
struct WorkRequest {RequestKey request;AppKind app=AppKind::NN;Digest model{},plan{},input_sha{};uint64_t input_bytes=0,queue_deadline_ns=0;std::shared_ptr<const WorkSource> source;uint64_t cpu_bytes=0,resident_bytes=0;};
}
