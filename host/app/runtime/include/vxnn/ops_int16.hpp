#pragma once
#include <vxnn/graph.hpp>
#include "device/vxnn/ops.h"
namespace vxnn {
// Internal trusted execution boundary. No callback, program or address comes
// from a model package or RPC peer. Native adapters supply the implementation.
class ExecutionContext {
public:
 virtual ~ExecutionContext()=default;
 virtual Result<Completion> submit(const PreparedNode&,uint32_t opcode)=0;
};
struct Int16Family {Digest program_sha{},implementation_sha{};};
OperatorKey int16_operator_key(uint32_t,const Int16Family&);
Result<void> register_int16_operators(OperatorRegistry&,const Int16Family&);
// Pure preparation of declared requirements, not physical identity admission.
// Actual Runtime/provider startup must still check full hardware/bit/XSA/epoch.
Result<Plan> prepare_int16_model(const ModelPackage&,const OperatorRegistry&,const DeviceCaps&);
Result<Plan> prepare_int16_model(const ModelPackage&,const OperatorRegistry&,const DeviceCaps&,const vxunified::BackendIdentity&);
}
