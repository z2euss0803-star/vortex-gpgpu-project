#include "mosaic_owner_profile_hooks.h"
#include "device/vxnn/mosaic_profile_ops.h"
#include <vxunified/mosaic_profile.hpp>
#include <uapi/vx_compute_v2.h>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
namespace {
void ck(bool ok,const char*why){if(!ok){std::fprintf(stderr,"OWNER512_HOOK_ASSERT %s\n",why);std::exit(78);}}
struct Copy {unsigned direction;uint64_t handle,generation,offset,bytes;bool zero;unsigned ack;};
struct Job {VxnnMosaicArgs args{};MosaicWorkLayoutV1 layout{};uint64_t handle=0,generation=0,offset=0;unsigned readmask=0;};
std::vector<Copy> copies;Job job;owner512_stats stats{};std::string mode;unsigned options;size_t cursor=0;bool finishing=false,matching=false;uint64_t guards_after_ack=0;std::array<unsigned char,256>last_snapshot{};
bool overlap(uint64_t a,uint64_t n,uint64_t b,uint64_t m){return a<b+m&&b<a+n;}
}
extern "C" void owner512_config(const char*m,unsigned o){mode=m;options=o;copies.clear();stats={};cursor=0;job={};}
extern "C" void owner512_copy(unsigned direction,uint64_t handle,uint64_t generation,uint64_t offset,uint64_t bytes,uint64_t user,unsigned ack,int rc){
 ck(!rc,"COPY returns original success");bool zero=true;
 if(direction==1){const auto*p=reinterpret_cast<const unsigned char*>(uintptr_t(user));for(uint64_t i=0;i<bytes;++i)if(p[i]){zero=false;break;}}
 copies.push_back({direction,handle,generation,offset,bytes,zero,ack});ck(copies.size()<100000,"bounded COPY event log");
 if(direction==2&&finishing&&bytes==64){++guards_after_ack;++stats.guard_reads;}
 if(direction!=2||!stats.jobs||handle!=job.handle||generation!=job.generation)return;
 for(unsigned core=0;core<4;++core){
  uint64_t expected=job.offset+64+uint64_t(core)*job.layout.core_stride+job.layout.profile_offset;
  if(!overlap(offset,bytes,expected,256))continue;
  ck(options==1&&job.args.reserved==1,"options0 performs no extra profile reads");
  ck(offset==expected&&bytes==256,"exact bounded256B per-core profile read");
  ck(finishing&&matching&&stats.acks>=stats.jobs&&guards_after_ack,"matching ACK/original snapshot and postACK guard reads precede profile read");
  ck(!(job.readmask&(1u<<core)),"each core profile read once");
  job.readmask|=1u<<core;++stats.profile_reads;
 }
}
extern "C" void owner512_inject(const void*opaque,void*work,uint64_t bytes,uint64_t handle,uint64_t generation,uint64_t offset){
 const auto&a=*static_cast<const VxnnMosaicArgs*>(opaque);VxnnMosaicShape shape{};
 MosaicWorkLayoutV1 layout{};ck((a.abi==3||a.abi==4)&&!vxunified::mosaic_profile_layout(a,true,&shape,&layout),"actual explicit ABI3/4 validator and profile layout");
 ck(bytes==layout.total_work_bytes,"exact production WORK extent");
 ck(a.reserved==options,"bound package options");
 unsigned header_mask=0,header_count=0;
 for(size_t i=cursor;i<copies.size();++i){const auto&e=copies[i];if(e.direction!=1||e.handle!=handle||e.generation!=generation)continue;
  for(unsigned core=0;core<4;++core){uint64_t at=offset+64+uint64_t(core)*layout.core_stride+layout.profile_offset;if(!overlap(e.offset,e.bytes,at,256))continue;
   ck(options==1&&e.offset==at&&e.bytes==64&&e.zero,"only opt1 invalidates exact64B profile header, no whole-slot zero");
   ck(!(header_mask&(1u<<core)),"one invalidation per core");ck(e.ack==stats.jobs,"header invalidation follows prior matching ACK");header_mask|=1u<<core;++header_count;
  }
 }
 ck(header_count==(options?4u:0u),"options0 no clear; options1 four header invalidations");stats.header_writes+=header_count;
 cursor=copies.size();job={};job.args=a;job.layout=layout;job.handle=handle;job.generation=generation;job.offset=offset;++stats.jobs;
 if(!options)return;
 bool zeroed=false;
 for(unsigned core=0;core<4;++core){
  auto*p=reinterpret_cast<MosaicProfileRecordV1*>(static_cast<unsigned char*>(work)+64+uint64_t(core)*layout.core_stride+layout.profile_offset);
  for(unsigned i=0;i<64;++i)ck(reinterpret_cast<unsigned char*>(p)[i]==0,"header physically invalid before HOST injection");
  mosaic_profile_initialize(p,a,layout,core);p->header.status=MOSAIC_PROFILE_COMPLETE_V1;uint32_t owned=0,packs=0,weights=0,last_nb=UINT32_MAX,last_mb=UINT32_MAX;
  const uint32_t spatial=((a.h*a.upsample+a.stride-1)/a.stride)*((a.w*a.upsample+a.stride-1)/a.stride),mblocks=(a.co+7)/8,nblocks=(spatial+7)/8;
  for(uint32_t tile=a.first;tile<a.first+a.count;++tile){
   const uint32_t nb=a.opcode==2?(a.abi==4?tile/mblocks:tile%nblocks):0,mb=a.opcode==2?(a.abi==4?tile%mblocks:tile/nblocks):0;
   const uint32_t owner=a.opcode==2&&a.abi==4?nb%4:tile%4;
   if(owner!=core)continue;
   ++owned;
   if(a.opcode==2){if(a.abi==3||nb!=last_nb){++packs;last_nb=nb;}if(mb!=last_mb){++weights;last_mb=mb;}}
  }
  // Explicit HOST-INJECTED format values, never sampled CSR cycles.
  if(owned&&a.opcode==2){
   for(unsigned stage=0;stage<5;++stage){auto&v=p->stage[stage];v.cycles=1000+stage*17+core;v.calls=owned;v.units=owned;v.logical_bytes=owned*16;p->header.valid_stages|=1u<<stage;}
   p->stage[0].calls=packs;p->stage[1].calls=weights;p->stage[2].units=uint64_t(owned)*a.ci*a.k*a.k;p->stage[3].units=uint64_t(owned)*24;
   stats.input_packs+=packs;stats.weight_prepares+=weights;stats.tiles+=owned;
  }else if(owned){p->header.valid_stages=1u<<5;auto&v=p->stage[5];v.cycles=2000+core;v.calls=1;v.units=owned;v.logical_bytes=uint64_t(owned)*256;}
  if(mode=="zero-csr"&&owned&&!zeroed){auto&v=p->stage[a.opcode==2?0:5];ck(v.calls>0,"zero-CSR fixture has real modeled calls");v.cycles=0;zeroed=true;}
  if(core==2&&mode=="bad-core")++p->header.core;
  if(core==2&&mode=="bad-tag")p->header.argument_tag^=1;
  if(core==2&&mode=="bad-version")++p->header.version;
  ++stats.injected;
 }
}
extern "C" void owner512_ack(unsigned n){ck(n==stats.acks+1,"ACK once per started job");stats.acks=n;}
extern "C" void owner512_snapshot(const void*p){std::memcpy(last_snapshot.data(),p,256);const auto&c=*static_cast<const vxl_v2_completion*>(p);ck(!c.cycles_valid&&!c.elapsed_raw,"HOST completion has no valid measured cycles");++stats.snapshots;}
extern "C" void owner512_finish_begin(int valid){finishing=true;matching=valid;guards_after_ack=0;}
extern "C" void owner512_finish_end(){finishing=false;}
extern "C" void owner512_trace(uint64_t index,int has_records){ck(index+1==stats.jobs&&stats.acks==stats.jobs,"trace after own ACK");ck(bool(has_records)==bool(options),"trace profile presence matches option");ck(job.readmask==(options?15u:0u),"all4 profile reads before trace, none for option0");++stats.traces;}
extern "C" owner512_stats owner512_get(){return stats;}
extern "C" void owner512_print(){std::printf("OWNER512_MOCK_WORK abi=%u input_packs=%llu weight_prepares=%llu tiles=%llu source=HOST_INJECTED_NOT_GPU\n",job.args.abi,(unsigned long long)stats.input_packs,(unsigned long long)stats.weight_prepares,(unsigned long long)stats.tiles);std::printf("OWNER512_EVENTS jobs=%llu ack=%llu snapshot=%llu header64_writes=%llu profile256_reads=%llu postACK_guard64_reads=%llu injected_records=%llu traces=%llu scope=HOST_PROTOCOL_TEST profiling=HOST_INJECTED_NOT_CSR cycles_valid=0\n",(unsigned long long)stats.jobs,(unsigned long long)stats.acks,(unsigned long long)stats.snapshots,(unsigned long long)stats.header_writes,(unsigned long long)stats.profile_reads,(unsigned long long)stats.guard_reads,(unsigned long long)stats.injected,(unsigned long long)stats.traces);}

extern "C" int owner512_snapshot_matches(const void*p){return p&&std::memcmp(last_snapshot.data(),p,256)==0;}

extern "C" unsigned owner512_current_abi(){return job.args.abi;}
