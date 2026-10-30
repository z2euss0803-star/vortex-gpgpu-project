#include <vxnn/mosaic_registry.hpp>
#include <cstdio>
using namespace vxnn;
int main(){
 DeviceCaps caps;caps.gpu_address_bits=32;caps.ps_dma_bits=32;caps.pl_address_bits=40;caps.cycle_counter_bits=32;
 ScalarParams p{{"shape",Shape{3,5,8,12,3,1,1,32}},{"flags",uint64_t(0)},{"scale_bits",uint64_t(0x3f800000)},{"budget",uint64_t(3)},{"work_layout_version",uint64_t(1)},{"profiling_options",uint64_t(1)}};
 TensorDesc input;input.id=1;input.dtype=DType::FP32;input.layout="CHW_F32";input.shape={3,8,12};input.bytes=1152;input.access=3;
 auto result=inspect_mosaic_operator(1,{input},p,caps);
 if(!result){std::fprintf(stderr,"EXPECTED_RED_ABI3_HOST_INSPECT_NOT_IMPLEMENTED %s\n",result.error->detail.c_str());return 73;}
 return result.value->args.abi==3&&result.value->args.extent[VXM_WORK]==1088?0:74;
}
