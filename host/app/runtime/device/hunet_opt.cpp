#include <vx_spawn.h>
#include <utility>
#include <vx_tensor.h>
#include "hunet_opt_gn_runtime.h"
#include "hunet_opt_gn_norm.h"
#include "hunet_opt_scalar.h"
#include <vxl/hunet_opt_wire.h>
#include "hunet_opt_pack.h"
#include "hunet_opt_wmma.h"
#ifndef HUNET_OPT_TILE_N
#define HUNET_OPT_TILE_N 1
#endif
#ifndef HUNET_OPT_INTERNAL_TIMING
#define HUNET_OPT_INTERNAL_TIMING 0
#endif
#ifndef HUNET_OPT_VIEWS
#define HUNET_OPT_VIEWS 0
#endif
#if HUNET_OPT_VIEWS
#ifndef HUNET_OPT_VIEWS_HEADER
#define HUNET_OPT_VIEWS_HEADER "../generated/hunet_opt/G3_VIEWS/views.h"
#endif
#ifndef HUNET_OPT_GRAPH_HEADER
#define HUNET_OPT_GRAPH_HEADER "../generated/hunet_opt/G3_VIEWS/device_graph.h"
#endif
#include HUNET_OPT_VIEWS_HEADER
#include HUNET_OPT_GRAPH_HEADER
#endif
#ifndef HUNET_OPT_LOCATE
#define HUNET_OPT_LOCATE 0
#endif
#ifndef HUNET_OPT_PACK
#define HUNET_OPT_PACK 0
#endif
#ifndef HUNET_OPT_GN
#define HUNET_OPT_GN 1
#endif
#ifndef HUNET_OPT_SCALAR_CHUNK
#define HUNET_OPT_SCALAR_CHUNK 0
#endif
extern int hunet_fused_entry();
namespace vt=vortex::tensor;
using OptWMMA=vt::wmma_context<4,vt::fp16,vt::fp32>;
static_assert(OptWMMA::tileM==8&&OptWMMA::tileN==4&&OptWMMA::tileK==8,"frozen HW4");
static const HunetFusedDescriptorV1 *opt_descriptors(const HunetFusedStageV1 *s){return reinterpret_cast<const HunetFusedDescriptorV1*>(uintptr_t(s->stage_descriptors_gpu));}
static int opt_select_descriptor(const HunetFusedStageV1*s,uint32_t grid,bool pack){
 const auto *ds=opt_descriptors(s);
 if(HUNET_OPT_LOCATE)return opt_find_descriptor(ds,s->descriptor_count,grid,pack);
 for(uint32_t i=0;i<s->descriptor_count;i++){uint32_t base=opt_descriptor_base(ds[i],pack);if(grid>=base&&uint64_t(grid)-base<opt_descriptor_span(ds[i],pack))return int(i);}
 return -1;
}
struct OptMatrixRun {HunetFusedStageV1 stage;uint32_t tile_n,groups,base[33],view_block,view_node;};
static void opt_pack_body(OptMatrixRun * __UNIFORM__ run){
 HunetFusedStageV1 *s=&run->stage;
 int i=opt_select_descriptor(s,blockIdx.x,true);if(i<0)return;const auto&d=opt_descriptors(s)[i];
 const auto*x=reinterpret_cast<const float*>(uintptr_t(s->input_gpu));auto*y=reinterpret_cast<uint16_t*>(uintptr_t(s->packed_b_gpu+d.packed_b_offset));
#if HUNET_OPT_VIEWS
 const auto*view=opt_bound_view(run->view_block,run->view_node);
 if(view){auto read=[](uint32_t address){return *reinterpret_cast<const float*>(uintptr_t(address));};opt_pack_row_view(*s,d,*view,read,y,blockIdx.x-d.pack_grid_base,threadIdx.x,blockDim.x);return;}
#endif
 if(HUNET_OPT_PACK)opt_pack_row(*s,d,x,y,blockIdx.x-d.pack_grid_base,threadIdx.x,blockDim.x);
 else hunet_fused_pack_row(*s,d,x,y,blockIdx.x-d.pack_grid_base,threadIdx.x,blockDim.x);
}
static uint64_t opt_cycle_now(){uint32_t hi,lo,again;do{hi=csr_read(VX_CSR_MCYCLE_H);lo=csr_read(VX_CSR_MCYCLE);again=csr_read(VX_CSR_MCYCLE_H);}while(hi!=again);return (uint64_t(hi)<<32)|lo;}

struct OptWmmaPort {
 using A=OptWMMA::fragment_a;using B=OptWMMA::fragment_b;using C=OptWMMA::fragment_acc;
 const HunetFusedStageV1 &s;const HunetFusedDescriptorV1 &d;uint64_t started;
 void fill(C&a){OptWMMA::fill_fragment(a,0.0f);}
 void load_a(A&a,uint32_t row,uint32_t k){const auto*w=reinterpret_cast<const uint16_t*>(uintptr_t(s.weights_gpu));OptWMMA::load_matrix_sync(a,w+row*d.kp+k,d.kp);}
 void load_b(B&b,uint32_t k,uint32_t col){const auto*x=reinterpret_cast<const uint16_t*>(uintptr_t(s.packed_b_gpu+d.packed_b_offset));OptWMMA::load_matrix_sync(b,x+k*d.np+col,d.np);}
 void mma(C&a,const A&w,const B&b,unsigned which,uint32_t k){OptWMMA::mma_sync(a,w,b,a);(void)which;(void)k;}
 void store(const C&a,uint32_t row,uint32_t col,uint32_t tile){
  auto*out=reinterpret_cast<float*>(uintptr_t(s.output_gpu));const auto*bias=reinterpret_cast<const float*>(uintptr_t(s.bias_gpu));
  vt::detail::unroll_for<8>([&](auto reg){hunet_fused_store(d,out,bias,row,col,threadIdx.x,reg,a.data[reg]);});
  if(threadIdx.x==0)reinterpret_cast<uint32_t*>(uintptr_t(s.owner_trace_gpu))[d.gemm_grid_base+tile]=vx_core_id()+1;
 }
 void start(){if(HUNET_OPT_INTERNAL_TIMING&&threadIdx.x==0)started=opt_cycle_now();}
 void stop(unsigned field){if(HUNET_OPT_INTERNAL_TIMING&&threadIdx.x==0){auto*p=reinterpret_cast<OptFusedTimingPage*>(uintptr_t(0x84141000));p->warp_activity[vx_core_id()][vx_warp_id()][field]+=opt_cycle_now()-started;}}
 void loop_begin(){start();}void loop_end(){stop(0);}void store_begin(){start();}void store_end(){stop(1);}
};
static void opt_grouped_gemm_body(OptMatrixRun * __UNIFORM__ run){
 uint32_t grid=blockIdx.x;int found=opt_find_group(run->base,run->stage.descriptor_count,grid);if(found<0)return;
 uint32_t i=uint32_t(found);const auto&d=opt_descriptors(&run->stage)[i];uint32_t group=grid-run->base[i],per_row=(d.np/4+run->tile_n-1)/run->tile_n;
 OptWmmaPort p{run->stage,d,0};uint32_t row=group/per_row*8,colgroup=group%per_row;
 if(run->tile_n==4)opt_wmma_group<4>(d,row,colgroup,threadIdx.x,p);
 else if(run->tile_n==2)opt_wmma_group<2>(d,row,colgroup,threadIdx.x,p);
 else opt_wmma_group<1>(d,row,colgroup,threadIdx.x,p);
}
static int hunet_opt_gemm_run_view(const HunetFusedStageV1 *s,uint32_t tile_n,uint32_t view_block,uint32_t view_node){
 OptMatrixRun run={};run.stage=*s;run.tile_n=tile_n;run.view_block=view_block;run.view_node=view_node;auto &local=run.stage;const auto*ds=opt_descriptors(&local);
 if((tile_n!=1&&tile_n!=2&&tile_n!=4)||local.magic!=VXL_FUSED_MAGIC||local.abi!=1||local.bytes!=128||local.op!=15||local.policy!=1||local.reserved||!opt_descriptor_table_valid(ds,local.descriptor_count)||!local.pack_grid_rows||!local.gemm_grid_tiles||local.owner_trace_elements!=local.gemm_grid_tiles||vx_num_cores()!=4||vx_num_warps()!=4||vx_num_threads()!=4)return 1;
 run.groups=opt_group_prefixes(ds,local.descriptor_count,tile_n,run.base);if(!run.groups)return 1;uint32_t core=vx_core_id();
 auto*timing=reinterpret_cast<OptFusedTimingPage*>(uintptr_t(0x84141000));
 if(HUNET_OPT_INTERNAL_TIMING){
  for(unsigned i=0;i<64;i++)if(timing->guard_left[i]!=0xa5||timing->guard_right[i]!=0xa5)return 1;
  for(unsigned warp=0;warp<4;warp++)for(unsigned f=0;f<2;f++)timing->warp_activity[core][warp][f]=0;
 }
 uint64_t begin=HUNET_OPT_INTERNAL_TIMING?opt_cycle_now():0;uint32_t block=4;
 int pack_rc=vx_spawn_threads(1,&local.pack_grid_rows,&block,(vx_kernel_func_cb)opt_pack_body,&run);
 if(HUNET_OPT_INTERNAL_TIMING){timing->core_wall[core][0]=opt_cycle_now()-begin;timing->core_rc[core][0]=pack_rc;begin=opt_cycle_now();}
 __asm__ volatile("fence iorw, iorw" ::: "memory");vx_barrier(0x80000000,4);
 if(HUNET_OPT_INTERNAL_TIMING)timing->core_wall[core][1]=opt_cycle_now()-begin;
 int gemm_rc=vx_spawn_threads(1,&run.groups,&block,(vx_kernel_func_cb)opt_grouped_gemm_body,&run);
 if(HUNET_OPT_INTERNAL_TIMING)timing->core_rc[core][1]=gemm_rc;
 __asm__ volatile("fence iorw, iorw" ::: "memory");vx_barrier(0x80000000,4);
 if(HUNET_OPT_INTERNAL_TIMING)for(unsigned i=0;i<64;i++)if(timing->guard_left[i]!=0xa5||timing->guard_right[i]!=0xa5)return 1;
 return pack_rc?pack_rc:gemm_rc;
}
int hunet_opt_gemm_run(const HunetFusedStageV1 *s,uint32_t tile_n){return hunet_opt_gemm_run_view(s,tile_n,0,0);}
int hunet_opt_fused_run(const HunetFusedStageV1 *s){return hunet_opt_gemm_run(s,HUNET_OPT_TILE_N);}
static void opt_gn_mean_body(OptGnRun * __UNIFORM__ r){opt_gn_worker(r,blockIdx.x*blockDim.x+threadIdx.x,false);}
static void opt_gn_variance_body(OptGnRun * __UNIFORM__ r){opt_gn_worker(r,blockIdx.x*blockDim.x+threadIdx.x,true);}
struct OptVxPort {
 uint32_t core(){return vx_core_id();}
 void fence(){__asm__ volatile("fence iorw, iorw" ::: "memory");}
 void barrier(uint32_t token,uint32_t cores){vx_barrier(token,cores);}
 uint64_t cycles(){return opt_cycle_now();}
 int spawn(OptGnRun *r,bool variance){
  uint32_t grid=(r->workers+3)/4,block=4;
  return vx_spawn_threads(1,&grid,&block,variance?(vx_kernel_func_cb)opt_gn_variance_body:(vx_kernel_func_cb)opt_gn_mean_body,r);
 }
};
int hunet_opt_gn_run(const HunetJobV1 *j){
 HunetJobV1 local=*j; // MSCRATCH is overwritten by spawn; retain a private copy.
 if(local.magic!=HUNET_MAGIC||local.abi!=1||local.bytes!=128||local.op!=HUNET_GN_STATS||vx_num_cores()!=4||vx_num_warps()!=4||vx_num_threads()!=4)return 1;
 OptVxPort p;return opt_gn_phases(local,reinterpret_cast<const float*>(uintptr_t(local.x)),reinterpret_cast<float*>(uintptr_t(local.y)),reinterpret_cast<OptGnScratch*>(uintptr_t(0x86c4df40)),p);
}
static void opt_norm_body(OptGnNormRun * __UNIFORM__ run){opt_gn_norm_worker(run,blockIdx.x*blockDim.x+threadIdx.x);}
struct OptNormPort:OptVxPort {
 int spawn_norm(OptGnNormRun *r){uint32_t grid=(r->gn.workers+3)/4,block=4;return vx_spawn_threads(1,&grid,&block,(vx_kernel_func_cb)opt_norm_body,r);}
};
int hunet_opt_gn_norm_run(const vxl_opt_graph_job_v2 *input){
 vxl_opt_graph_job_v2 job=*input;
 if(job.magic!=VXL_OPT_JOB_MAGIC||job.version!=2||job.bytes!=128||job.opcode!=16||!job.block_id||job.block_id>64||job.descriptor_gpu!=0x84000000+(job.block_id-1)*4096||job.descriptor_bytes!=320||job.policy_id!=2||job.scratch_gpu!=0x86c4df40||job.scratch_bytes!=4096||vx_num_cores()!=4||vx_num_warps()!=4||vx_num_threads()!=4)return 1;
 const auto*head=reinterpret_cast<const vxl_opt_graph_header_v2*>(uintptr_t(job.descriptor_gpu));
 if(head->magic!=VXL_OPT_DESC_MAGIC||head->version!=2||head->bytes!=320||head->block_id!=job.block_id||head->node_count!=2||head->first!=job.first||head->last!=job.last||head->first_stage!=job.first_stage||head->last_stage!=job.last_stage)return 1;
 const auto*nodes=reinterpret_cast<const vxl_opt_graph_node_v2*>(head+1);const auto a=nodes[0],b=nodes[1];
 if(a.type!=4||b.type!=5||a.last+1!=b.first||a.input0!=b.input0||a.output!=b.aux||a.input0==b.output||a.ci!=b.ci||a.ih!=b.ih||a.iw!=b.iw)return 1;
 HunetJobV1 stats={};stats.magic=HUNET_MAGIC;stats.abi=1;stats.bytes=128;stats.op=4;stats.sequence=a.first;stats.x=a.input0;stats.y=a.output;stats.ci=a.ci;stats.co=a.co;stats.ih=a.ih;stats.iw=a.iw;stats.oh=a.oh;stats.ow=a.ow;stats.epsilon_bits=0x3727c5ac;
 OptNormPort p;return opt_gn_norm_phases(stats,reinterpret_cast<const float*>(uintptr_t(a.input0)),reinterpret_cast<float*>(uintptr_t(b.output)),reinterpret_cast<float*>(uintptr_t(a.output)),reinterpret_cast<const float*>(uintptr_t(b.gamma)),reinterpret_cast<const float*>(uintptr_t(b.beta)),reinterpret_cast<OptGnScratch*>(uintptr_t(job.scratch_gpu)),p);
}
#if HUNET_OPT_VIEWS
int hunet_opt_graph_run(const vxl_opt_graph_job_v2 *input){
 vxl_opt_graph_job_v2 job=*input;const auto*bound=opt_bound_graph(job.block_id);
 if(!bound||job.opcode!=17||__builtin_memcmp(&job,bound->job,128)||vx_num_cores()!=4||vx_num_warps()!=4||vx_num_threads()!=4)return 1;
 const auto*header=reinterpret_cast<const vxl_opt_graph_header_v2*>(uintptr_t(job.descriptor_gpu));
 if(__builtin_memcmp(header,bound->descriptor,bound->bytes))return 1;
 const auto*nodes=reinterpret_cast<const vxl_opt_graph_node_v2*>(header+1);
 auto*control=reinterpret_cast<OptFusedTimingPage*>(uintptr_t(0x84141000));uint32_t owner_offset=0;
 for(unsigned i=0;i<64;i++)if(control->guard_left[i]!=0xa5||control->guard_right[i]!=0xa5)return 1;
 for(uint32_t index=0;index<header->node_count;index++){
  const auto node=nodes[index];int rc=0;
  if(opt_virtual_node(job.block_id,index)){
   if(node.type!=12&&node.type!=13&&node.type!=7)return 1;
  }else if(node.type==15){
   const auto*matrix=opt_bound_matrix(node.stage);const auto*view=opt_bound_view(job.block_id,index);
   if(!matrix||!view||view->channels!=matrix->ci||view->height!=matrix->ih||view->width!=matrix->iw)return 1;
   HunetFusedStageV1 local=*matrix;local.owner_trace_gpu=job.owner_trace_gpu+owner_offset*4;
   rc=hunet_opt_gemm_run_view(&local,HUNET_OPT_TILE_N,job.block_id,index);owner_offset+=local.gemm_grid_tiles;
  }else return 1; // This entry accepts only statically compiled closed view blocks.
  control->core_rc[vx_core_id()][0]=uint32_t(rc);
  __asm__ volatile("fence iorw, iorw" ::: "memory");vx_barrier(0x80000000,4);
  for(unsigned i=0;i<64;i++)if(control->guard_left[i]!=0xa5||control->guard_right[i]!=0xa5)return 1;
  for(unsigned core=0;core<4;core++)if(control->core_rc[core][0])return 1;
 }
 return owner_offset==job.owner_trace_elements?0:1;
}
#endif
struct OptScalarRun {HunetJobV1 job;uint32_t chunk;};
static void opt_scalar_body(OptScalarRun * __UNIFORM__ run){
 auto range=opt_scalar_range(run->job.tasks,blockIdx.x,threadIdx.x,run->chunk);int error=range.valid?opt_scalar_span(run->job,range.first,range.end,4):0;
 if(error){auto*control=reinterpret_cast<OptFusedTimingPage*>(uintptr_t(0x84141000));control->scalar_error[vx_core_id()][vx_warp_id()][vx_thread_id()]|=uint32_t(error);}
}
int hunet_opt_scalar_run(const HunetJobV1 *input){
 OptScalarRun run={*input,HUNET_OPT_SCALAR_CHUNK};uint32_t grid=opt_scalar_grid(run.job.tasks,run.chunk),block=4,core=vx_core_id();
 if(!grid||!opt_scalar_job_valid(run.job)||vx_num_cores()!=4||vx_num_warps()!=4||vx_num_threads()!=4)return 1;
 auto*control=reinterpret_cast<OptFusedTimingPage*>(uintptr_t(0x84141000));
 for(unsigned i=0;i<64;i++)if(control->guard_left[i]!=0xa5||control->guard_right[i]!=0xa5)return 1;
 for(unsigned warp=0;warp<4;warp++)for(unsigned lane=0;lane<4;lane++)control->scalar_error[core][warp][lane]=0;
 __asm__ volatile("fence iorw, iorw" ::: "memory");
 int rc=vx_spawn_threads(1,&grid,&block,(vx_kernel_func_cb)opt_scalar_body,&run);control->core_rc[core][0]=uint32_t(rc);
 __asm__ volatile("fence iorw, iorw" ::: "memory");vx_barrier(0x80000000,4);
 uint32_t result=0;for(unsigned c=0;c<4;c++){result|=control->core_rc[c][0];for(unsigned w=0;w<4;w++)for(unsigned lane=0;lane<4;lane++)result|=control->scalar_error[c][w][lane];}
 for(unsigned i=0;i<64;i++)if(control->guard_left[i]!=0xa5||control->guard_right[i]!=0xa5)result|=1;
 return int(result);
}
int main(){
 const auto *j=reinterpret_cast<const HunetJobV1*>(uintptr_t(csr_read(VX_CSR_MSCRATCH)));
 if(j->op==16)return hunet_opt_gn_norm_run(reinterpret_cast<const vxl_opt_graph_job_v2*>(j));
 if(HUNET_OPT_SCALAR_CHUNK&&opt_scalar_supported(j->op))return hunet_opt_scalar_run(j);
#if HUNET_OPT_VIEWS
 if(j->op==17)return hunet_opt_graph_run(reinterpret_cast<const vxl_opt_graph_job_v2*>(j));
#endif
 if(HUNET_OPT_GN&&j->op==HUNET_GN_STATS)return hunet_opt_gn_run(j);
 if((HUNET_OPT_LOCATE||HUNET_OPT_PACK||HUNET_OPT_TILE_N!=1||HUNET_OPT_INTERNAL_TIMING)&&j->op==15)return hunet_opt_fused_run(reinterpret_cast<const HunetFusedStageV1*>(j));
 return hunet_fused_entry();
}
