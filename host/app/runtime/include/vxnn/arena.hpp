#pragma once
#include <vxnn/graph.hpp>
namespace vxnn {
// Internal opt-in planning. Existing plan_memory, wire ABI and program keys
// keep their original semantics. All offsets below are domain/object-relative.
struct ArenaObject {uint64_t slot=0;AddressDomain domain=AddressDomain::PL;uint64_t offset=0,bytes=0;uint32_t access=3;};
struct ArenaView {TensorBinding tensor;uint64_t arena=0,object_offset=0,guard_before=0,guard_after=0,guard_bytes=64;uint32_t access=0;};
struct ArenaPlan {
 Digest plan_id{};std::vector<ArenaObject> objects;std::vector<ArenaView> tensors;
 std::map<NodeId,ArenaView> workspaces;
 std::optional<ArenaView> readonly_placeholder;
 uint64_t cpu_peak=0,ps_peak=0,pl_peak=0;uint32_t object_count=0;
};
struct ArenaAllocation {uint64_t arena=0;AddressDomain domain=AddressDomain::PL;BufferRef backing;};
struct ArenaBindings {BindingSet tensors;std::map<NodeId,BufferRef> workspaces;std::optional<BufferRef> readonly_placeholder;};
Result<ArenaPlan> plan_arenas(const Plan&,const MemoryBudget&);
// Only bind already-owned allocations; no allocate/copy/ioctl/address guessing.
// CPU arenas need a separate host-storage provider and are refused here.
Result<ArenaBindings> bind_arenas(const ArenaPlan&,const std::vector<ArenaAllocation>&,uint64_t expected_device_generation);
}
