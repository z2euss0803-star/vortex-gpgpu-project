#pragma once
#include <vxnn/ops_int16.hpp>
#include <vxnn/arena_storage.hpp>
namespace vxnn {
struct Int16Lowered {NodeId node=0;vxnn_i16_job_v1 args{};std::array<BufferRef,3>references{};};
// Pure lowering. The private payload sequence is the application node ordinal,
// not the kernel submission sequence; the original completion records the latter.
// Addresses stay zero until the existing C schema relocates owned references.
Result<Int16Lowered>lower_int16_node(const Plan&,const Int16Family&,const ArenaStorage&,NodeId,uint64_t logical_sequence,uint64_t cookie);
}
