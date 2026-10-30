#include "device/vxnn/mosaic_wire.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <climits>
#include <initializer_list>
static void check(bool v,const char*why){if(!v){std::fprintf(stderr,"U13_MOSAIC_WIRE_ASSERT %s\n",why);std::exit(77);}}
static VxnnMosaicArgs base(){VxnnMosaicArgs a{};a.abi=1;a.bytes=128;a.opcode=2;a.ci=3;a.co=32;a.h=960;a.w=1280;a.k=9;a.stride=1;a.upsample=1;a.kpad=256;a.count=4;a.scale_bits=0x3f800000;return a;}
static void set_sizes(VxnnMosaicArgs&a){for(auto&n:a.extent)n=64;a.extent[VXM_OUTPUT]=157286400;a.extent[VXM_QUANT]=7372800;a.extent[VXM_WEIGHTS]=16384;a.extent[VXM_COEFF]=256;a.extent[VXM_WORK]=34880;}
static void relocate(VxnnMosaicArgs&a,const VxnnMosaicShape&s){
 a.address[VXM_WORK]=0x80800000;uint64_t next=uint64_t(a.address[VXM_WORK])+a.extent[VXM_WORK]+64;
#ifndef VX_MOSAIC_LEGACY_V1
 const uint32_t readonly_dummy=uint32_t((next+63)/64*64);next=uint64_t(readonly_dummy)+128;
#endif
 for(unsigned i=0;i<7;i++){if(s.used_mask&(1u<<i)){next=(next+63)/64*64;a.address[i]=uint32_t(next);next+=a.extent[i]+64;}else{
#ifdef VX_MOSAIC_LEGACY_V1
  a.address[i]=a.address[VXM_WORK];
#else
  a.address[i]=(i==VXM_INPUT||i==VXM_WEIGHTS||i==VXM_COEFF)?readonly_dummy:a.address[VXM_WORK];
#endif
 }}
}
int main(){
 auto a=base();set_sizes(a);VxnnMosaicShape s{};check(!vxnn_mosaic_validate(&a,false,&s),"full first convolution private metadata");check(s.items==614400&&s.core_work_bytes==8704&&s.used_mask==((1u<<1)|(1u<<2)|(1u<<3)|(1u<<4)|(1u<<7)),"hand-counted first tile geometry/work");
 check(s.required[VXM_OUTPUT]==157286400&&s.required[VXM_QUANT]==7372800&&s.required[VXM_WEIGHTS]==16384&&s.required[VXM_COEFF]==256&&s.required[VXM_WORK]==34880,"hand-counted byte extents");
 auto placed=a;relocate(placed,s);check(!vxnn_mosaic_validate(&placed,true,&s),"bounded relocated eight refs and canonical dummies");check(vxnn_mosaic_validate(&placed,false,&s)<0,"CPU preparation never accepts GPU pointers");check(vxnn_mosaic_validate(&a,true,&s)<0,"target never accepts unresolved pointers");
 for(unsigned role=0;role<8;role++){
  auto bad=a;bad.extent[role]++;check(vxnn_mosaic_validate(&bad,false,&s)<0,"every extent exact, including inactive roles");bad=a;bad.address[role]=0x80800000;check(vxnn_mosaic_validate(&bad,false,&s)<0,"every pre-relocation address zero");bad=placed;bad.address[role]=0;check(vxnn_mosaic_validate(&bad,true,&s)<0,"every target reference resolved");
 }
 for(uint32_t bits:{0u,0x80000000u,0xbf800000u,0x7f800000u,0x7fc00001u}){auto bad=a;bad.scale_bits=bits;check(vxnn_mosaic_validate(&bad,false,&s)<0,"positive finite scale bit contract");}
 for(unsigned mutation=0;mutation<20;mutation++){
  auto bad=placed;
  switch(mutation){case 0:bad.abi=0;break;case 1:bad.bytes=176;break;case 2:bad.opcode=7;break;case 3:bad.flags=4;break;case 4:bad.flags=1;break;case 5:bad.reserved=1;break;case 6:bad.count=4097;break;case 7:bad.first=UINT_MAX;break;case 8:bad.count=0;break;case 9:bad.kpad=255;break;case 10:bad.ci=128;break;case 11:bad.h=961;break;case 12:bad.address[VXM_OUTPUT]=0xfff00000;break;case 13:bad.address[VXM_OUTPUT]++;break;case 14:bad.address[VXM_OUTPUT]=bad.address[VXM_QUANT];break;case 15:bad.extent[VXM_OUTPUT]--;break;case 16:bad.extent[VXM_INPUT]=0;break;case 17:
#ifdef VX_MOSAIC_LEGACY_V1
   bad.address[VXM_INPUT]+=64; // v1 requires the exact WORK prefix.
#else
   bad.address[VXM_INPUT]+=4; // v2 independent RO64 must be64-byte aligned.
#endif
   break;case 18:bad.scale_bits=0x7f800000;break;case 19:bad.scale_bits=0x80000000;break;}
  VxnnMosaicShape sentinel;memset(&sentinel,0xa5,sizeof(sentinel));auto before=sentinel;check(vxnn_mosaic_validate(&bad,true,&sentinel)<0&&!memcmp(&sentinel,&before,sizeof(before)),"bad args rejected without partial output");
 }
#ifndef VX_MOSAIC_LEGACY_V1
 for(unsigned role:{unsigned(VXM_INPUT),unsigned(VXM_WEIGHTS),unsigned(VXM_COEFF)})if(!(s.used_mask&(1u<<role))){auto bad=placed;bad.address[role]=bad.address[VXM_WORK];check(vxnn_mosaic_validate(&bad,true,&s)<0,"v2 unused readonly cannot alias writable WORK");bad=placed;bad.address[role]=bad.address[VXM_WEIGHTS];check(vxnn_mosaic_validate(&bad,true,&s)<0,"v2 unused readonly cannot overlap active weights");}
#endif
 auto q=base();q.opcode=1;q.flags=1;for(auto&n:q.extent)n=64;q.extent[VXM_INPUT]=14745600;q.extent[VXM_QUANT]=7372800;q.extent[VXM_SKIP]=14745600;q.count=4096;
 check(!vxnn_mosaic_validate(&q,false,&s)&&s.items==14400&&s.required[VXM_WORK]==64,"quant/save original input extents");
 auto stats=base();stats.opcode=3;stats.flags=2;stats.scale_bits=0;for(auto&n:stats.extent)n=64;stats.extent[VXM_INPUT]=157286400;stats.extent[VXM_STATS]=384;stats.extent[VXM_WORK]=67108928;stats.count=32;
 check(!vxnn_mosaic_validate(&stats,false,&s)&&s.items==32&&s.norm_elements==2097152&&s.core_work_bytes==16777216,"full norm four-core tree uses checked size above64MiB");
 relocate(stats,s);check(!vxnn_mosaic_validate(&stats,true,&s),"norm work and tensor disjoint");
 auto apply=base();apply.opcode=4;apply.flags=2;apply.scale_bits=0;for(auto&n:apply.extent)n=64;apply.extent[VXM_INPUT]=apply.extent[VXM_OUTPUT]=157286400;apply.extent[VXM_COEFF]=512;apply.extent[VXM_STATS]=384;
 check(!vxnn_mosaic_validate(&apply,false,&s)&&s.items==153600,"apply coefficient block and channel spatial");relocate(apply,s);check(!vxnn_mosaic_validate(&apply,true,&s),"SSA input/output relocated separately");apply.address[VXM_OUTPUT]=apply.address[VXM_INPUT];check(vxnn_mosaic_validate(&apply,true,&s)<0,"SSA alias rejected");
 for(unsigned op:{5u,6u}){auto p=base();p.opcode=op;p.flags=2;p.scale_bits=0;for(auto&n:p.extent)n=64;p.extent[VXM_INPUT]=p.extent[VXM_OUTPUT]=157286400;if(op==6)p.extent[VXM_SKIP]=157286400;check(!vxnn_mosaic_validate(&p,false,&s),"scalar point private schema");relocate(p,s);check(!vxnn_mosaic_validate(&p,true,&s),"scalar eight typed references");}
 auto tail=a;tail.first=614399;tail.count=1;check(!vxnn_mosaic_validate(&tail,false,&s),"last tile allowed");tail.count=2;check(vxnn_mosaic_validate(&tail,false,&s)<0,"tail cannot exceed extent");
#ifdef VX_MOSAIC_LEGACY_V1
 std::puts("MOSAIC_LEGACY_V1_PRIVATE128_ORIGINAL_WORK_DUMMY_NEGATIVES_PASS_HOST_NOT_GPU gpu_raw_cycles=null");
#else
 std::puts("MOSAIC_RO64_V2_PRIVATE128_GEOMETRY_RELOCATION_NEGATIVES_PASS_HOST_NOT_GPU gpu_raw_cycles=null");
#endif
}
