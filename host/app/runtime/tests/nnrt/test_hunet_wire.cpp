#include "device/vxnn/hunet_wire.h"
#include "vendor/hunet/v5/include/hunet_job.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <vector>
static void need(bool b,const char*s){if(!b){std::fprintf(stderr,"U12_WIRE_ASSERT %s\n",s);std::exit(67);}}
template<class T>static std::vector<T>read(const char*p){std::ifstream f(p,std::ios::binary|std::ios::ate);need(bool(f),"open frozen wire");auto n=f.tellg();need(n>0&&n%sizeof(T)==0,"wire multiple");std::vector<T>v(size_t(n)/sizeof(T));f.seekg(0);f.read(reinterpret_cast<char*>(v.data()),n);need(bool(f),"complete frozen wire");return v;}
static void rejected(const VxnnHunetArgs&a,const HunetFusedDescriptorV1*d,size_t n,bool relocated,const char*label){VxnnHunetShape out;std::memset(&out,0xa5,sizeof(out));const auto before=out;need(vxnn_hunet_validate(&a,d,n,relocated,&out)<0,label);need(!std::memcmp(&out,&before,sizeof(out)),"invalid job cannot publish partially validated shape");}
int main(){
 auto jobs=read<HunetJobV1>("generated/hunet_fused/jobs.bin");auto ds=read<HunetFusedDescriptorV1>("generated/hunet_fused/descriptors.bin");unsigned matrix=0,scalar=0,descriptors=0;
 for(const auto&j:jobs){VxnnHunetArgs a{};a.abi=1;a.bytes=128;a.op=j.op;const HunetFusedDescriptorV1*d=nullptr;size_t nd=0;VxnnHunetShape expected{};
  if(j.op==15){HunetFusedStageV1 s;std::memcpy(&s,&j,128);a.flags=s.flags|(s.bias_gpu?16:0);a.ci=s.ci;a.co=s.co;a.ih=s.ih;a.iw=s.iw;a.oh=s.oh;a.ow=s.ow;a.k=s.kernel_k;a.stride=s.stride;a.pad=s.pad;
   need(s.stage_descriptors_gpu>=0x86d0df80&&(s.stage_descriptors_gpu-0x86d0df80)%64==0,"independent original descriptor offset");size_t base=(s.stage_descriptors_gpu-0x86d0df80)/64;nd=s.descriptor_count;need(base+nd<=ds.size(),"original descriptor bound");d=ds.data()+base;expected.required[VXH_W]=d->mp*d->kp*2;expected.required[VXH_BIAS]=s.bias_gpu?d->m*4:0;expected.required[VXH_STATS]=nd*64;expected.required[VXH_Y]=d->m*d->n*4;expected.pack_rows=s.pack_grid_rows;expected.gemm_tiles=s.gemm_grid_tiles;expected.tasks=s.gemm_grid_tiles;expected.packed_bytes=s.packed_b_bytes;expected.trace_offset=(s.packed_b_bytes+63)&~63u;expected.required[VXH_WORK]=expected.trace_offset+s.owner_trace_elements*4;expected.descriptor_count=nd;matrix++;descriptors+=nd;
  }else{a.ci=j.ci;a.co=j.co;a.ih=j.ih;a.iw=j.iw;a.oh=j.oh;a.ow=j.ow;a.k=j.k;a.stride=j.stride;a.pad=j.pad;a.groups=j.groups;a.n_div=j.n_div;a.epsilon_bits=j.epsilon_bits;expected.required[VXH_Y]=(j.op==HUNET_GN_STATS?2:j.co*j.oh*j.ow)*4;expected.tasks=j.tasks;expected.required[VXH_WORK]=16;
   if(j.op==HUNET_ADD)expected.required[VXH_W]=expected.required[VXH_Y];
   if(j.op==HUNET_MUL_CHANNEL)expected.required[VXH_W]=j.co*4;
   if(j.op==HUNET_CONCAT)expected.required[VXH_W]=(j.co-j.ci)*j.ih*j.iw*4;
   if(j.op==HUNET_GN_APPLY){expected.required[VXH_GAMMA]=expected.required[VXH_BETA]=j.ci*4;expected.required[VXH_STATS]=8;}
   scalar++;
  }
  expected.required[VXH_X]=a.ci*a.ih*a.iw*4;for(unsigned i=0;i<6;i++)if(!expected.required[i])expected.required[i]=expected.required[VXH_X];for(unsigned i=0;i<8;i++)a.extent[i]=expected.required[i];
  VxnnHunetShape out{};need(!vxnn_hunet_validate(&a,d,nd,false,&out),"complete new controlled wire accepts original frozen job");need(!std::memcmp(&out,&expected,sizeof(out)),"derived extents grids tasks layout equal independent original wire");
  auto bad=a;bad.abi=2;rejected(bad,d,nd,false,"wrong ABI");bad=a;bad.bytes=124;rejected(bad,d,nd,false,"wrong byte count");bad=a;bad.op=6;rejected(bad,d,nd,false,"CPU GELU cannot enter GPU library");bad=a;bad.flags|=0x80000000;rejected(bad,d,nd,false,"unknown flags");bad=a;bad.ci=UINT32_MAX;rejected(bad,d,nd,false,"shape overflow");
  for(unsigned i=0;i<8;i++){bad=a;bad.extent[i]--;rejected(bad,d,nd,false,"short extent");bad=a;bad.address[i]=0x80800000;rejected(bad,d,nd,false,"userspace cannot inject raw address before relocation");}
  auto relocated=a;uint32_t next=0x80800000;for(unsigned i=0;i<8;i++){bool alias=i<6&&a.extent[i]==a.extent[0]&&((i==VXH_BIAS&&!(a.flags&16))||(i==VXH_W&&a.op!=15&&a.op!=8&&a.op!=11&&a.op!=13)||(i==VXH_GAMMA&&a.op!=5)||(i==VXH_BETA&&a.op!=5)||(i==VXH_STATS&&a.op!=5&&a.op!=15));relocated.address[i]=alias?relocated.address[0]:next;if(!alias)next+=((a.extent[i]+63)&~63u);}
  need(!vxnn_hunet_validate(&relocated,d,nd,true,&out),"relocated bounded eight refs accepted");bad=relocated;bad.address[VXH_Y]=bad.address[VXH_X];rejected(bad,d,nd,true,"output cannot alias any input");bad=relocated;bad.address[VXH_WORK]=bad.address[VXH_Y];rejected(bad,d,nd,true,"workspace cannot alias output");bad=relocated;bad.address[VXH_Y]=0xfffffff0;rejected(bad,d,nd,true,"GPU32 wrapped or stack crossing span");bad=relocated;bad.address[VXH_X]=0x80000000;rejected(bad,d,nd,true,"CODE region cannot be data");
  if(d){bad=relocated;bad.address[VXH_W]+=2;rejected(bad,d,nd,true,"WMMA needs aligned32 load even for FP16 storage");std::vector<HunetFusedDescriptorV1> mutate(d,d+nd);mutate[0].reserved[0]=1;rejected(a,mutate.data(),nd,false,"descriptor reserved");mutate.assign(d,d+nd);mutate[0].np+=4;rejected(a,mutate.data(),nd,false,"noncanonical padding");mutate.assign(d,d+nd);mutate[0].packed_b_offset+=2;rejected(a,mutate.data(),nd,false,"packed gap");mutate.assign(d,d+nd);mutate[0].gemm_grid_base++;rejected(a,mutate.data(),nd,false,"grid gap");mutate.assign(d,d+nd);mutate.back().count--;rejected(a,mutate.data(),nd,false,"incomplete output coverage");rejected(a,d,nd-1,false,"short descriptor reference");}
 }
 need(matrix==35&&scalar==68&&descriptors==314,"all frozen jobs and descriptors covered");puts("U12_CONTROLLED_ARGS8_103_JOBS_314_DESCRIPTORS_BOUNDS_PASS_NOT_GPU gpu_raw_cycles=null");
}
