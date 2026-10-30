#pragma once
#include <vxnn/ops_int16.hpp>
#include "device/vxnn/mosaic_wire.h"
namespace vxnn {
struct MosaicFamily {Digest program_sha{},readonly_sha{},schema_sha{},implementation_sha{};};
struct MosaicOperand {uint32_t role=0;DType type=DType::FP32;std::string layout;Shape shape;uint64_t bytes=0;bool constant=false;};
struct MosaicInvocation {
 VxnnMosaicArgs args{};VxnnMosaicShape shape{};
 std::vector<MosaicOperand>inputs,outputs;uint32_t budget=0;uint64_t jobs=0;
};
OperatorKey mosaic_operator_key(uint32_t,const MosaicFamily&);
Result<void> register_mosaic_operators(OperatorRegistry&,const MosaicFamily&);
// Metadata inspection does not admit an unpublished program. The family overload
// additionally prevents old CODE/schema from receiving cooperative parameters.
Result<MosaicInvocation> inspect_mosaic_operator(uint32_t,const std::vector<TensorDesc>&,const ScalarParams&,const DeviceCaps&);
Result<MosaicInvocation> inspect_mosaic_operator(uint32_t,const std::vector<TensorDesc>&,const ScalarParams&,const DeviceCaps&,const MosaicFamily&);
}
