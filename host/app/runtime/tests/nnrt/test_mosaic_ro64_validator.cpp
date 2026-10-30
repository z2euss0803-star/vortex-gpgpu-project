#include "device/vxnn/mosaic_wire.h"
#include <cassert>
#include <cerrno>
#include <cstdio>
int main(){
 VxnnMosaicArgs a{};a.abi=1;a.bytes=128;a.opcode=1;a.ci=3;a.co=32;a.h=8;a.w=12;a.k=9;a.stride=a.upsample=1;a.kpad=256;a.count=1;a.scale_bits=0x3f800000;
 for(auto&n:a.extent){n=64;}a.extent[VXM_INPUT]=1152;a.extent[VXM_QUANT]=576;
 for(auto&n:a.address){n=0x80803000;}a.address[VXM_INPUT]=0x80801000;a.address[VXM_QUANT]=0x80802000;a.address[VXM_WEIGHTS]=a.address[VXM_COEFF]=0x80804000;VxnnMosaicShape shape{};
#ifdef EXPECT_LEGACY_REJECTION
 assert(vxnn_mosaic_validate(&a,true,&shape)==-EINVAL);puts("FROZEN_V1_REJECTS_DRIVER_SAFE_RO64_REPRODUCED_NOT_GPU");return 0;
#else
 assert(!vxnn_mosaic_validate(&a,true,&shape));auto bad=a;bad.address[VXM_WEIGHTS]=bad.address[VXM_COEFF]=bad.address[VXM_WORK];assert(vxnn_mosaic_validate(&bad,true,&shape)==-EINVAL);
 bad=a;bad.address[VXM_WEIGHTS]+=4;assert(vxnn_mosaic_validate(&bad,true,&shape)==-EINVAL);bad=a;bad.address[VXM_WEIGHTS]=0x807fffc0;assert(vxnn_mosaic_validate(&bad,true,&shape)==-EINVAL);bad=a;bad.address[VXM_COEFF]+=64;assert(vxnn_mosaic_validate(&bad,true,&shape)==-EINVAL);
 bad=a;bad.address[VXM_WEIGHTS]=bad.address[VXM_COEFF]=bad.address[VXM_INPUT];assert(vxnn_mosaic_validate(&bad,true,&shape)==-EINVAL);bad=a;bad.address[VXM_OUTPUT]+=64;assert(vxnn_mosaic_validate(&bad,true,&shape)==-EINVAL);bad=a;bad.address[VXM_QUANT]=bad.address[VXM_INPUT];assert(vxnn_mosaic_validate(&bad,true,&shape)==-EINVAL);
 puts("RO64_V2_BOUNDED_UNUSED_RO_AND_ALL_WRITE_DISJOINT_VALIDATOR_PASS_HOST_NOT_GPU");
#endif
}
