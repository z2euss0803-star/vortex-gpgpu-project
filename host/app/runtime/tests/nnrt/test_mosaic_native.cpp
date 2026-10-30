#include "device/vxnn/mosaic_wire.h"
#include <cstdio>
#include <cstring>
extern "C" int mosaic_native_fixture(const char*,const char*,const char*,const uint32_t*,uint32_t,uint32_t*);
#define CHECK(x,s) do{if(!(x)){std::fprintf(stderr,"U13_MOSAIC_NATIVE_ASSERT %s\n",s);return 79;}}while(0)
int main(int argc,char**argv){
 CHECK(argc==4,"three artifact paths");
 for(unsigned op=1;op<=6;op++)for(unsigned save=0;save<(op==1?2u:1u);save++){
  VxnnMosaicArgs a{};a.abi=1;a.bytes=128;a.opcode=op;a.flags=op<3?save:2;a.ci=3;a.co=5;a.h=8;a.w=12;a.k=3;a.stride=1;a.upsample=1;a.kpad=32;a.count=1;if(op<3)a.scale_bits=0x3f800000;
  for(auto&v:a.extent)v=64;
  if(op==1){a.extent[VXM_INPUT]=1152;a.extent[VXM_QUANT]=576;if(save)a.extent[VXM_SKIP]=1152;}
  if(op==2){a.extent[VXM_OUTPUT]=1920;a.extent[VXM_QUANT]=576;a.extent[VXM_WEIGHTS]=320;a.extent[VXM_COEFF]=40;a.extent[VXM_WORK]=6208;}
  if(op==3){a.extent[VXM_INPUT]=1920;a.extent[VXM_STATS]=60;a.extent[VXM_WORK]=4160;}
  if(op>=4){a.extent[VXM_INPUT]=1920;a.extent[VXM_OUTPUT]=1920;}
  if(op==4){a.extent[VXM_COEFF]=80;a.extent[VXM_STATS]=60;}
  if(op==6)a.extent[VXM_SKIP]=1920;
  VxnnMosaicShape shape{};CHECK(!vxnn_mosaic_validate(&a,false,&shape),"canonical original packet");
  uint32_t raw[32]{};CHECK(!mosaic_native_fixture(argv[1],argv[2],argv[3],reinterpret_cast<const uint32_t*>(&a),shape.used_mask,raw),"actual CODE RO load and relocation");
  VxnnMosaicArgs relocated;std::memcpy(&relocated,raw,128);CHECK(!vxnn_mosaic_validate(&relocated,true,&shape),"all eight relocated refs satisfy exact native predicate");
  CHECK(relocated.address[VXM_WORK]>=0x80800000u,"DATA outside code/readonly/private reservations");
 }
#ifdef VX_MOSAIC_LEGACY_V1
 std::puts("MOSAIC_LEGACY_V1_ACTUAL_CODE_RO_C_LOAD_ORIGINAL_DUMMY_PASS_HOST_NO_GPU gpu_raw_cycles=null");
#else
 std::puts("MOSAIC_RO64_V2_ACTUAL_CODE_RO_C_LOAD_REF_MUTATIONS_AND_RETIREMENT_PASS_HOST_NO_GPU gpu_raw_cycles=null");
#endif
 return 0;
}
