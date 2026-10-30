#pragma once
#include <vxnn/hunet_registry.hpp>
#include <vxnn/arena_storage.hpp>
namespace vxnn {
struct HunetLowered {
 NodeId node=0;VxnnHunetArgs args{};std::array<BufferRef,8> references{};
};
// Trusted Plan only; no program handle, catalog entry ID, address or I/O here.
// Actual residency is resolved by the owner at dispatch, never at admission.
Result<HunetLowered> lower_hunet_node(const Plan&,const HunetFamily&,const ArenaStorage&,NodeId);
}
