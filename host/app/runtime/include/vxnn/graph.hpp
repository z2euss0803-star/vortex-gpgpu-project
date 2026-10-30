#pragma once
#include <vxnn/package.hpp>
#include <vxunified/types.hpp>
namespace vxnn {
struct OptimizationPolicy {bool fusion=false,tiling=false,constant_reuse=false,inplace=false;std::string shape_profile;vxunified::BackendIdentity hardware;};
struct PlannedTensor {TensorDesc desc;uint64_t first=0,last=0;};
struct PlannedNode {PreparedNode node;uint64_t job_count=0,evidence_bytes=0,workspace_bytes=0;};
struct Plan {
 Digest model_id{},plan_id{},numeric_id{};OptimizationPolicy policy;DeviceCaps caps;
 std::vector<PlannedNode> nodes;std::vector<PlannedTensor> tensors;std::vector<TensorId> inputs,outputs;
 std::vector<vxunified::ProgramKey> programs;
 uint64_t total_jobs=0,evidence_bytes=0,resident_bytes=0;
 // Private full-request read-only placeholder; never aliases writable WORK.
 uint64_t readonly_placeholder_bytes=0;
};
struct HeldRange {AddressDomain domain=AddressDomain::PL;uint64_t offset=0,bytes=0;bool retired=false;};
struct MemoryBudget {
 uint64_t cpu_bytes=1ULL<<30,ps_bytes=0,pl_bytes=0,ps_reserved=0,pl_reserved=0;
 uint32_t resident_models=1,existing_objects=0,max_objects=64;
 std::vector<HeldRange> held;
};
struct TensorBinding {TensorId tensor=0;AddressDomain domain=AddressDomain::PL;uint64_t offset=0,bytes=0,slot=0;};
struct MemoryPlan {Digest plan_id{};std::vector<TensorBinding> bindings;std::map<NodeId,TensorBinding> workspace_bindings;uint64_t cpu_peak=0,ps_peak=0,pl_peak=0;uint32_t object_count=0;};
using BindingSet=std::map<TensorId,BufferRef>;
Result<Plan> prepare_graph(const ModelPackage&,const OperatorRegistry&,const DeviceCaps&,OptimizationPolicy);
Result<MemoryPlan> plan_memory(const Plan&,const MemoryBudget&);
}
