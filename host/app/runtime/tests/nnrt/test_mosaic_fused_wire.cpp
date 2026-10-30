#include "device/vxnn/mosaic_wire.h"
#include <cassert>
#include <cstdio>
static VxnnMosaicArgs args(unsigned op,unsigned save){
 VxnnMosaicArgs a{};a.abi=2;a.bytes=128;a.opcode=op;a.flags=save;a.ci=1;a.co=3;a.h=8;a.w=12;a.k=3;a.stride=1;a.upsample=1;a.kpad=32;a.count=2;a.scale_bits=0x3f800000;for(auto&x:a.extent)x=64;a.extent[VXM_INPUT]=1152;a.extent[VXM_QUANT]=576;a.extent[VXM_COEFF]=48;a.extent[VXM_STATS]=36;if(save)a.extent[VXM_OUTPUT]=1152;if(op==8)a.extent[VXM_SKIP]=1152;return a;
}
int main(){unsigned cases=0;for(unsigned op:{7u,8u})for(unsigned save:{0u,1u}){
 auto a=args(op,save);VxnnMosaicShape shape{};assert(!vxnn_mosaic_validate_fused(&a,false,&shape)&&shape.items==2&&shape.core_work_bytes==0);assert(vxnn_mosaic_validate(&a,false,&shape));
 for(unsigned role=0;role<8;role++)a.address[role]=(shape.used_mask&(1u<<role))?0x81010000+role*0x10000:(role==VXM_WEIGHTS?0x81000000:0x81080000);
 a.address[VXM_WORK]=0x81080000;
 assert(!vxnn_mosaic_validate_fused(&a,true,&shape));auto bad=a;bad.abi=1;assert(vxnn_mosaic_validate_fused(&bad,true,&shape));bad=a;bad.flags=2;assert(vxnn_mosaic_validate_fused(&bad,true,&shape));bad=a;bad.count=3;assert(vxnn_mosaic_validate_fused(&bad,true,&shape));bad=a;bad.scale_bits=0x7fc00000;assert(vxnn_mosaic_validate_fused(&bad,true,&shape));bad=a;bad.address[VXM_QUANT]=bad.address[VXM_INPUT];assert(vxnn_mosaic_validate_fused(&bad,true,&shape));bad=a;bad.first=1;bad.count=1;assert(!vxnn_mosaic_validate_fused(&bad,true,&shape));if(op==8&&save){bad=a;bad.address[VXM_SKIP]=bad.address[VXM_OUTPUT];assert(vxnn_mosaic_validate_fused(&bad,true,&shape));}++cases;
 }std::printf("MOSAIC_FUSED_ABI2_EXTENTS_TAILS_READONLY_ALIAS_AND_OLD_ABI_ISOLATION_PASS_HOST_ONLY cases=%u\n",cases);}
