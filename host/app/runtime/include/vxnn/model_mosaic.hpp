#pragma once
#include <vxnn/mosaic_registry.hpp>
namespace vxnn {
Result<Plan> prepare_mosaic(const ModelPackage&,const OperatorRegistry&,const DeviceCaps&);
Result<Plan> prepare_mosaic(const ModelPackage&,const OperatorRegistry&,const DeviceCaps&,const vxunified::BackendIdentity&);
}
