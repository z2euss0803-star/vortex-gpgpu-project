#include "device/vxnn/mosaic_wire.h"
#include <cstdio>
int main(){
 VxnnMosaicArgs a{};a.abi=3;a.bytes=128;a.opcode=1;a.ci=3;a.co=5;a.h=8;a.w=12;a.k=3;a.stride=a.upsample=1;a.kpad=32;a.count=1;a.scale_bits=0x3f800000;
 for(auto &n:a.extent){n=64;}a.extent[VXM_INPUT]=1152;a.extent[VXM_QUANT]=576;a.extent[VXM_WORK]=1088;
 VxnnMosaicShape s{};int rc=vxnn_mosaic_validate_fused(&a,false,&s);
 if(rc){std::fprintf(stderr,"EXPECTED_RED_ABI3_POINT_WORK1088_NOT_IMPLEMENTED rc=%d\n",rc);return 73;}
 return 0;
}

