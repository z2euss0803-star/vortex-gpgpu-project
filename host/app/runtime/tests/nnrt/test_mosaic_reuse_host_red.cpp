#include <vxnn/mosaic_registry.hpp>
#include <cstdio>
using namespace vxnn;
static TensorDesc tensor(unsigned id,DType dtype,const char*layout,Shape shape,bool constant=false){TensorDesc t;t.id=id;t.dtype=dtype;t.layout=layout;t.shape=shape;t.bytes=dtype==DType::I16?2:4;for(auto n:shape)t.bytes*=n;t.access=constant?1:3;t.constant=constant;if(constant){t.asset="HOST_METADATA_ONLY";t.content_sha=content_digest("host metadata");}return t;}
int main(){DeviceCaps caps;caps.gpu_address_bits=caps.ps_dma_bits=caps.cycle_counter_bits=32;caps.pl_address_bits=40;
 ScalarParams p{{"shape",Shape{1,3,8,12,3,1,1,32}},{"flags",uint64_t(0)},{"scale_bits",uint64_t(0x3f800000)},{"budget",uint64_t(3)},{"work_layout_version",uint64_t(1)},{"profiling_options",uint64_t(0)},{"schedule_version",uint64_t(2)},{"weight_layout_version",uint64_t(2)}};
 auto result=inspect_mosaic_operator(2,{tensor(1,DType::I16,"CHW_I16",{1,8,12}),tensor(2,DType::I16,"MB_KPAD_ROW8_I16_LE_V1",{1,32,8},true),tensor(3,DType::FP32,"MOSAIC_COEFF_F32",{6},true)},p,caps);
 if(!result){std::fprintf(stderr,"EXPECTED_RED_ABI4_EXPLICIT_LAYOUT2_INSPECT_MISSING %s\n",result.error->detail.c_str());return 73;}return result.value->args.abi==4&&result.value->args.extent[VXM_WEIGHTS]==512?0:74;}
