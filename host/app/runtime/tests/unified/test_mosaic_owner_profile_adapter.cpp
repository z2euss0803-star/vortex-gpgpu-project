#include <vxunified/mosaic_profile.hpp>
#include "tests/unified/mosaic_owner_profile_hooks.h"
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
using namespace vxunified;
namespace {
uint64_t checks=0,samples=0,unavailable=0;
void ck(bool ok,const char*why){++checks;if(!ok){std::fprintf(stderr,"OWNER512_ADAPTER_ASSERT %s checks=%llu\n",why,(unsigned long long)checks);std::exit(78);}}
unsigned used(unsigned op){
 unsigned m=1u<<VXM_WORK;
 if(op==1)return m|(1u<<VXM_INPUT)|(1u<<VXM_QUANT);
 if(op==2)return m|(1u<<VXM_OUTPUT)|(1u<<VXM_QUANT)|(1u<<VXM_WEIGHTS)|(1u<<VXM_COEFF);
 if(op==3)return m|(1u<<VXM_INPUT)|(1u<<VXM_STATS);
 if(op==4)return m|(1u<<VXM_INPUT)|(1u<<VXM_OUTPUT)|(1u<<VXM_COEFF)|(1u<<VXM_STATS);
 if(op==5)return m|(1u<<VXM_INPUT)|(1u<<VXM_OUTPUT);
 if(op==6)return m|(1u<<VXM_INPUT)|(1u<<VXM_OUTPUT)|(1u<<VXM_SKIP);
 return m|(1u<<VXM_INPUT)|(1u<<VXM_QUANT)|(1u<<VXM_COEFF)|(1u<<VXM_STATS)|(op==8?(1u<<VXM_SKIP):0);
}
VxnnMosaicArgs packet(unsigned abi,unsigned op,unsigned co,unsigned first,unsigned count,unsigned option){
 VxnnMosaicArgs a{};a.abi=abi;a.bytes=128;a.opcode=op;a.flags=op>=3&&op<=6?2:0;
 a.ci=3;a.co=co;a.h=8;a.w=12;a.k=3;a.stride=a.upsample=1;a.kpad=32;a.first=first;a.count=count;a.reserved=option;
 if(op<3||op>=7)a.scale_bits=0x3f800000;
 for(auto &n:a.extent)n=64;
 const uint32_t input=3*8*12,output=co*8*12;
 if(op==1){a.extent[VXM_INPUT]=input*4;a.extent[VXM_QUANT]=input*2;}
 if(op==2){a.extent[VXM_OUTPUT]=output*4;a.extent[VXM_QUANT]=input*2;a.extent[VXM_WEIGHTS]=abi==4?((co+7)/8)*32*16:co*32*2;a.extent[VXM_COEFF]=co*8;}
 if(op>=3&&op<=6){a.extent[VXM_INPUT]=output*4;if(op!=3)a.extent[VXM_OUTPUT]=output*4;}
 if(op==3)a.extent[VXM_STATS]=co*12;
 if(op==4||op>=7){a.extent[VXM_COEFF]=co*16;a.extent[VXM_STATS]=co*12;}
 if(op>=7){a.extent[VXM_INPUT]=output*4;a.extent[VXM_QUANT]=output*2;}
 if(op==6||op==8)a.extent[VXM_SKIP]=output*4;
 MosaicWorkLayoutV1 l{};uint32_t tree=op==3?128:0;
 ck(!(abi==4?mosaic_reuse_work_layout_v4(op,32,tree,&l):mosaic_work_layout_v1(op,32,tree,&l)),"derive actual WORK");
 a.extent[VXM_WORK]=l.total_work_bytes;VxnnMosaicShape s{};MosaicWorkLayoutV1 got{};
 ck(!mosaic_profile_layout(a,false,&s,&got),"underlying explicit ABI3/4 admitted");
 return a;
}
VxnnMosaicArgs relocate(VxnnMosaicArgs a){
 uint64_t next=0x80800000u;unsigned mask=used(a.opcode);
 for(unsigned role=0;role<8;++role)if(mask&(1u<<role)){a.address[role]=uint32_t(next);next=(next+a.extent[role]+63)/64*64;}
 for(unsigned role=0;role<8;++role)if(!(mask&(1u<<role)))a.address[role]=(role==VXM_INPUT||role==VXM_WEIGHTS||role==VXM_COEFF)?uint32_t(next):a.address[VXM_WORK];
 VxnnMosaicShape s{};MosaicWorkLayoutV1 l{};ck(!mosaic_profile_layout(a,true,&s,&l),"relocated exact extents including inactive RO");return a;
}
struct Context {
 VxnnMosaicArgs wire,native;MosaicWorkLayoutV1 layout{};std::vector<uint8_t>memory;unsigned reads=0;
 BufferRef ref{9,7,128,0,3,2};
 Context(VxnnMosaicArgs a,const char*mode,bool configure=true):wire(a),native(relocate(a)){
  VxnnMosaicShape s{};ck(!mosaic_profile_layout(wire,false,&s,&layout),"common production profile layout");
  ref.bytes=layout.total_work_bytes;memory.assign(layout.total_work_bytes,0xa5);
  if(configure)owner512_config(mode,a.reserved);
  const auto before=memory;
  if(a.reserved){
   std::array<uint8_t,64>zero{};auto stats=owner512_get();
   for(unsigned core=0;core<4;++core){uint64_t local=64+uint64_t(core)*layout.core_stride+layout.profile_offset;std::memset(memory.data()+local,0,64);
    owner512_copy(1,9,7,128+local,64,reinterpret_cast<uintptr_t>(zero.data()),unsigned(stats.acks),0);}
  }
  owner512_inject(&native,memory.data(),memory.size(),9,7,128);
  if(!a.reserved)ck(memory==before,"options0 inject performs zero memory writes");
  owner512_ack(unsigned(owner512_get().jobs));owner512_finish_begin(1);
  std::array<uint8_t,64>guard{};
  // Explicit simulated boundary for this format-only component test. The
  // full C owner/ACK/guard path waits for501 model admission.
  owner512_copy(2,99,1,0,64,reinterpret_cast<uintptr_t>(guard.data()),unsigned(owner512_get().acks),0);
 }
 MosaicProfileRecordV1&record(unsigned core){return *reinterpret_cast<MosaicProfileRecordV1*>(memory.data()+64+uint64_t(core)*layout.core_stride+layout.profile_offset);}
 Result<MosaicProfileRecords>read(){
  return mosaic_profile_read(wire,ref,[&](BufferRef r,vxnn::MutableBytes b){
   ck(r.handle==9&&r.object_generation==7&&r.device_generation==2&&r.access==1,"owned reference preserved");
   ck(b.size==256&&r.bytes==256&&r.offset==128+64+uint64_t(reads)*layout.core_stride+layout.profile_offset,"bounded ordered per-core slice");
   ++reads;owner512_copy(2,9,7,r.offset,r.bytes,reinterpret_cast<uintptr_t>(b.data),unsigned(owner512_get().acks),0);
   std::memcpy(b.data,memory.data()+r.offset-128,256);return Result<void>::success();
  });
 }
 ~Context(){owner512_finish_end();}
};
void sample(unsigned abi,unsigned op,unsigned co,unsigned first,unsigned count){
 auto a=packet(abi,op,co,first,count,1);Context c(a,"flow");auto got=c.read();
 ck(bool(got)&&c.reads==4,"HOST injected records accepted by real parser");
 for(unsigned core=0;core<4;++core){
  const auto&h=(*got.value)[core].header;ck(h.schedule_version==(abi==4?2u:1u)&&h.weight_layout_version==(abi==4?2u:1u),"explicit schedule/weight metadata");
 }
 owner512_trace(0,1);++samples;
}
void literal_spatial_owner(){
 // co24 =>3 output-channel blocks,12 spatial blocks; job[1,9) contains
 // nb0:{mb1,2},nb1:{0,1,2},nb2:{0,1,2}. Core3 owns nothing.
 Context c(packet(4,2,24,1,8,1),"flow");auto r=c.read();ck(bool(r),"multi-mb partial-boundary job valid");
 const unsigned tiles[4]={2,3,3,0},packs[4]={1,1,1,0},weights[4]={2,3,3,0};
 for(unsigned core=0;core<4;++core){const auto&s=(*r.value)[core].stage;
  ck(s[2].calls==tiles[core]&&s[0].calls==packs[core]&&s[1].calls==weights[core],"literal nb%4/Breuse/last_mb counts");
  if(tiles[core])ck(s[0].calls<s[2].calls,"input B packs lower than tiles");
 }
 auto stats=owner512_get();ck(stats.tiles==8&&stats.input_packs==3&&stats.weight_prepares==8,"literal whole job modeled work");
}
void second_literal_and_legacy(){
 {Context c(packet(4,2,32,0,20,1),"flow");auto r=c.read();ck(bool(r),"multi-nb core revisited");
  const unsigned tiles[4]={8,4,4,4},packs[4]={2,1,1,1};
  for(unsigned core=0;core<4;++core)ck((*r.value)[core].stage[2].calls==tiles[core]&&(*r.value)[core].stage[0].calls==packs[core]&&(*r.value)[core].stage[1].calls==tiles[core],"literal spatial-major4mb counts");}
 {Context c(packet(3,2,24,1,8,1),"flow");auto r=c.read();ck(bool(r),"legacy channel-major accepted explicitly");
  for(unsigned core=0;core<4;++core)ck((*r.value)[core].stage[2].calls==2&&(*r.value)[core].stage[0].calls==2&&(*r.value)[core].stage[1].calls==1,"literal ABI3 tile%4 remains separate");}
}
void no_cross_job_B_cache(){
 owner512_config("flow",1);
 for(unsigned first:{1u,2u}){
  Context c(packet(4,2,24,first,1,1),"flow",false);auto r=c.read();ck(bool(r),"split one-nb jobs valid");
  ck((*r.value)[0].stage[0].calls==1&&(*r.value)[0].stage[1].calls==1,"each job freshly prepares B and A");
 }
 auto stats=owner512_get();ck(stats.jobs==2&&stats.input_packs==2,"B cache never crosses job boundary");
}
void error_cases(){
 for(unsigned abi:{3u,4u})for(unsigned op:{1u,2u,3u,7u,8u}){
  {Context c(packet(abi,op,24,0,1,1),"zero-csr");auto r=c.read();
   ck(!r&&r.error->code==ENODATA&&r.error->category=="MOSAIC_PROFILE"&&c.reads==1,"calls>0 CSR0 means timing unavailable, never zero elapsed/math failure");++unavailable;}
  for(const char*bad:{"bad-core","bad-tag","bad-version"}){
   Context c(packet(abi,op,24,0,1,1),bad);auto r=c.read();
   ck(!r&&r.error->code==EPROTO&&c.reads==3,"first bad metadata stops bounded prefix");
  }
  {Context c(packet(abi,op,24,0,1,0),"flow");auto r=c.read();auto s=owner512_get();
   ck(!r&&r.error->code==EINVAL&&!c.reads&&!s.header_writes&&!s.injected&&!s.profile_reads,"disabled profile does not clear, inject or read");}
 }
 {Context c(packet(4,2,24,1,8,1),"flow");c.record(0).stage[0].calls=c.record(0).stage[2].calls;auto r=c.read();
  ck(!r&&r.error->code==EPROTO,"old one-pack-per-tile counts rejected for ABI4 reuse");}
 {Context c(packet(4,2,24,1,8,1),"flow");auto&r=c.record(3);r.header.valid_stages=1;r.stage[0].calls=1;r.stage[0].cycles=7;auto got=c.read();
  ck(!got&&got.error->code==EPROTO,"tile%4-inactive core cannot report work under nb%4");}
}
}
int main(){
 for(unsigned abi:{3u,4u})for(unsigned op:{1u,2u,3u,4u,5u,6u,7u,8u})sample(abi,op,24,0,1);
 literal_spatial_owner();second_literal_and_legacy();no_cross_job_B_cache();error_cases();
 std::printf("OWNER512_ADAPTER_ABI3_4_HOST_PASS checks=%llu samples=%llu zeroCSR_ENODATA=%llu\n",(unsigned long long)checks,(unsigned long long)samples,(unsigned long long)unavailable);
 std::puts("SIMULATED_BOUNDARY_LAYOUT_PARSER_INJECTION_ONLY FORMAL_V7_MODEL_NOT_GENERATED HOST_INJECTED_NOT_CSR cycles_valid=0 gpu_raw_cycles=null");return 0;
}
