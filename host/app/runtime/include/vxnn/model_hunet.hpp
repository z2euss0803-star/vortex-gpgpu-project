#pragma once
#include <vxnn/hunet_registry.hpp>
namespace vxnn {
Result<Plan> prepare_hunet(const ModelPackage&,const OperatorRegistry&,const DeviceCaps&);
Result<Plan> prepare_hunet(const ModelPackage&,const OperatorRegistry&,const DeviceCaps&,const vxunified::BackendIdentity&);
}
