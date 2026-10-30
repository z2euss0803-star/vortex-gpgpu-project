#pragma once
#include <vxnn/mosaic_registry.hpp>
#include <vxnn/arena_storage.hpp>
namespace vxnn {
struct MosaicLowered {NodeId node=0;uint64_t batch=0;VxnnMosaicArgs args{};std::array<BufferRef,8>references{};};
// Pure lowering from trusted typed graph and READY owned storage. Only the
// existing checked relocator creates device addresses; no I/O or handles guessed.
Result<MosaicLowered> lower_mosaic_batch(const Plan&,const MosaicFamily&,const ArenaStorage&,NodeId,uint64_t batch);
}
