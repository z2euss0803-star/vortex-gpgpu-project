#pragma once
#include <vxnn/ops_int16.hpp>
#include <cerrno>
#include "device/vxnn/hunet_wire.h"
namespace vxnn {
struct HunetFamily {Digest program_sha{},implementation_sha{},schema_sha{};};
struct HunetInvocation {
 VxnnHunetArgs args{};std::vector<HunetFusedDescriptorV1> descriptors;
 std::vector<uint32_t> read_roles;Shape output_shape;std::string output_layout;
 uint64_t workspace_bytes=0;bool cpu_gelu=false;
};
// Native implementation must use the existing shared pool/borrowed budget and
// finish verified GELU COMMIT before returning. Never route CPU opcode6 to GPU.
class HunetExecutionContext:public ExecutionContext {
public:virtual Result<Completion> cpu_gelu(const PreparedNode&)=0;
 virtual Result<Completion> cpu_gn_stats(const PreparedNode&){return Result<Completion>::failure({EOPNOTSUPP,"HUNET_CPU","ordered stats context required, no GPU fallback"});}
};
OperatorKey hunet_operator_key(uint32_t,const HunetFamily&);
OperatorKey hunet_stats_cpu_key(const HunetFamily&);
Result<void> register_hunet_operators(OperatorRegistry&,const HunetFamily&);
Result<HunetInvocation> inspect_hunet_operator(uint32_t,const std::vector<TensorDesc>&,const ScalarParams&,const DeviceCaps&);
}
