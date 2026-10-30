// NEW program0x10010; original program9 and its byte identity remain intact.
#include <utility>
#include <vx_spawn.h>
#include <vx_tensor.h>
#include "hunet_wire.h"
#include "hunet_ops.h"
#include "hunet_scalar_math.h"
#include "hunet_scalar_dispatch.h"
#include "device/hunet_fused_ops.h"
#include "hunet_grouped_gemm.h"
#ifndef VXNN_HUNET_TILE_N
#define VXNN_HUNET_TILE_N 1
#endif
static_assert(VXNN_HUNET_TILE_N==1||VXNN_HUNET_TILE_N==2,"controlled tile_n1/2");
namespace vt=vortex::tensor;
using WMMA=vt::wmma_context<4,vt::fp16,vt::fp32>;
static_assert(WMMA::tileM==8&&WMMA::tileN==4&&WMMA::tileK==8&&WMMA::fragment_acc::NR==8,"frozen W4 FP16 WMMA");
namespace {
struct Invocation {VxnnHunetArgs args;VxnnHunetShape shape;HunetFusedStageV1 fused;HunetJobV1 scalar;VxnnHunetScalarBuffers buffers;
#if VXNN_HUNET_TILE_N == 2
VxnnHunetGroupPlan grouped;
#endif
};
static __thread volatile uint32_t main_error;
void fence(){__asm__ volatile("fence iorw, iorw":::"memory");}
volatile uint32_t* error_word(uint32_t core){return reinterpret_cast<volatile uint32_t*>(uintptr_t(0x10002000u+core*256+64));}
void fail(){*error_word(vx_core_id())=1;}
bool rounding_ok(){uint32_t f;__asm__ volatile("csrr %0, fcsr":"=r"(f));return (f&0xe0u)==0;}
bool failed(){fence();for(uint32_t c=0;c<4;c++)if(*error_word(c))return true;return false;}
const HunetFusedDescriptorV1* descriptors(const Invocation*c){return reinterpret_cast<const HunetFusedDescriptorV1*>(uintptr_t(c->args.address[VXH_STATS]));}
void hunet_controlled_pack(void*p){
 auto*c=static_cast<Invocation*>(p);if(!rounding_ok()){fail();return;}const auto*ds=descriptors(c);const auto&s=c->fused;
 for(uint32_t i=0;i<s.descriptor_count;i++){const auto&d=ds[i];if(blockIdx.x>=d.pack_grid_base&&blockIdx.x-d.pack_grid_base<d.kp){
  hunet_fused_pack_row(s,d,reinterpret_cast<const float*>(uintptr_t(s.input_gpu)),reinterpret_cast<uint16_t*>(uintptr_t(s.packed_b_gpu+d.packed_b_offset)),blockIdx.x-d.pack_grid_base,threadIdx.x,blockDim.x);return;
 }}fail();
}
#if VXNN_HUNET_TILE_N == 2
struct ControlledWmmaPort {
 using A=WMMA::fragment_a;using B=WMMA::fragment_b;using C=WMMA::fragment_acc;
 const HunetFusedStageV1&s;const HunetFusedDescriptorV1&d;
 const uint16_t*weights;const uint16_t*packed;uint32_t leading_k,leading_n;
 ControlledWmmaPort(const HunetFusedStageV1&stage,const HunetFusedDescriptorV1&desc):s(stage),d(desc),weights(reinterpret_cast<const uint16_t*>(uintptr_t(stage.weights_gpu))),packed(reinterpret_cast<const uint16_t*>(uintptr_t(stage.packed_b_gpu+desc.packed_b_offset))),leading_k(desc.kp),leading_n(desc.np){}
 void fill(C&a){WMMA::fill_fragment(a,0.0f);}
 void load_a(A&a,uint32_t row,uint32_t k){WMMA::load_matrix_sync(a,weights+row*leading_k+k,leading_k);}
 void load_b(B&b,uint32_t k,uint32_t col){WMMA::load_matrix_sync(b,packed+k*leading_n+col,leading_n);}
 void mma(C&a,const A&w,const B&b,uint32_t){WMMA::mma_sync(a,w,b,a);}
 void store(const C&a,uint32_t row,uint32_t col,uint32_t tile){
  auto*out=reinterpret_cast<float*>(uintptr_t(s.output_gpu));const auto*bias=reinterpret_cast<const float*>(uintptr_t(s.bias_gpu));
  vt::detail::unroll_for<8>([&](auto reg){const auto r=hunet_fused_acc_row(row,threadIdx.x,reg),n=hunet_fused_acc_col(col,threadIdx.x,reg);if(r<d.m&&n<d.count){float value=bias?a.data[reg]+bias[r]:a.data[reg];if(!isfinite(value))fail();}hunet_fused_store(d,out,bias,row,col,threadIdx.x,reg,a.data[reg]);});
  if(threadIdx.x==0)reinterpret_cast<uint32_t*>(uintptr_t(s.owner_trace_gpu))[d.gemm_grid_base+tile]=vx_core_id()+1;
 }
};
void hunet_controlled_gemm(void*p){
 auto*c=static_cast<Invocation*>(p);if(!rounding_ok()){fail();return;}
 int i=vxnn_hunet_group_find(c->grouped,blockIdx.x);if(i<0){fail();return;}
 const auto&d=descriptors(c)[i];uint32_t local=blockIdx.x-c->grouped.prefix[i],per_row=(d.np/4+1)/2;
 ControlledWmmaPort port{c->fused,d};vxnn_hunet_group_compute<2>(d,(local/per_row)*8,local%per_row,port);
 if(!rounding_ok())fail();
}
#else
void hunet_controlled_gemm(void*p){
 auto*c=static_cast<Invocation*>(p);if(!rounding_ok()){fail();return;}const auto&s=c->fused;const auto*ds=descriptors(c);
 for(uint32_t i=0;i<s.descriptor_count;i++){const auto&d=ds[i];const uint32_t tiles=(d.mp/8)*(d.np/4);if(blockIdx.x<d.gemm_grid_base||blockIdx.x-d.gemm_grid_base>=tiles)continue;
  uint32_t tile=blockIdx.x-d.gemm_grid_base,row=(tile/(d.np/4))*8,col=(tile%(d.np/4))*4;
  const auto*w=reinterpret_cast<const uint16_t*>(uintptr_t(s.weights_gpu));const auto*b=reinterpret_cast<const uint16_t*>(uintptr_t(s.packed_b_gpu+d.packed_b_offset));
  WMMA::fragment_a fw;WMMA::fragment_b fb;WMMA::fragment_acc acc;WMMA::fill_fragment(acc,0.0f);
  // Preserve frozen increasing-K accumulation, no split-K or parallel sum.
  for(uint32_t k=0;k<d.kp;k+=8){WMMA::load_matrix_sync(fw,w+row*d.kp+k,d.kp);WMMA::load_matrix_sync(fb,b+k*d.np+col,d.np);WMMA::mma_sync(acc,fw,fb,acc);}
  auto*out=reinterpret_cast<float*>(uintptr_t(s.output_gpu));auto*bias=reinterpret_cast<const float*>(uintptr_t(s.bias_gpu));
  vt::detail::unroll_for<8>([&](auto reg){const auto r=hunet_fused_acc_row(row,threadIdx.x,reg),n=hunet_fused_acc_col(col,threadIdx.x,reg);if(r<d.m&&n<d.count){float value=bias?acc.data[reg]+bias[r]:acc.data[reg];if(!isfinite(value))fail();}hunet_fused_store(d,out,bias,row,col,threadIdx.x,reg,acc.data[reg]);});
  if(threadIdx.x==0)reinterpret_cast<uint32_t*>(uintptr_t(s.owner_trace_gpu))[blockIdx.x]=vx_core_id()+1;
  if(!rounding_ok())fail();return;
 }fail();
}

#endif

void hunet_controlled_scalar(void*p){
 auto*c=static_cast<Invocation*>(p);const uint32_t t=blockIdx.x*4+threadIdx.x;if(t>=c->shape.tasks)return;if(!rounding_ok()){fail();return;}
 vxnn_hunet_scalar_math(&c->scalar,t,c->buffers);
 if(c->args.op==HUNET_GN_STATS){if(!isfinite(c->buffers.y.data[0])||!isfinite(c->buffers.y.data[1]))fail();}
 else if(!isfinite(c->buffers.y.data[t]))fail();
 if(!rounding_ok())fail();
}
// Separate entries keep each operation's locals in its own callback. Each lane
// owns a disjoint stride-four span, including row/channel and final tails.
#define VXNN_HUNET_SPAN_BODY(name,span) \
void name(void*p){ \
 auto*c=static_cast<Invocation*>(p);const uint32_t width=4*vxnn_hunet_span_chunk(&c->scalar),first=blockIdx.x*width; \
 if(first+threadIdx.x>=c->shape.tasks)return;if(!rounding_ok()){fail();return;} \
 if(!span(&c->scalar,c->buffers,first+threadIdx.x,vxnn_hunet_span_end(&c->scalar,first,width),4))fail(); \
 if(!rounding_ok())fail(); \
}
VXNN_HUNET_SPAN_BODY(hunet_controlled_gn_apply,vxnn_hunet_gn_apply_span)
VXNN_HUNET_SPAN_BODY(hunet_controlled_add,vxnn_hunet_add_span)
VXNN_HUNET_SPAN_BODY(hunet_controlled_mul,vxnn_hunet_mul_span)
VXNN_HUNET_SPAN_BODY(hunet_controlled_copy,vxnn_hunet_copy_span)
VXNN_HUNET_SPAN_BODY(hunet_controlled_slice,vxnn_hunet_slice_span)
VXNN_HUNET_SPAN_BODY(hunet_controlled_concat,vxnn_hunet_concat_span)
VXNN_HUNET_SPAN_BODY(hunet_controlled_shift,vxnn_hunet_shift_span)
#undef VXNN_HUNET_SPAN_BODY
void hunet_controlled_gn_stats(void*p){
 auto*c=static_cast<Invocation*>(p);if(blockIdx.x*4+threadIdx.x>=c->shape.tasks)return;if(!rounding_ok()){fail();return;}
 if(!vxnn_hunet_gn_stats_ordered(&c->scalar,c->buffers))fail();if(!rounding_ok())fail();
}
void hunet_controlled_gap(void*p){
 auto*c=static_cast<Invocation*>(p);const uint32_t t=blockIdx.x*4+threadIdx.x;if(t>=c->shape.tasks)return;if(!rounding_ok()){fail();return;}
 if(!vxnn_hunet_gap_ordered(&c->scalar,c->buffers,t))fail();if(!rounding_ok())fail();
}
}
int main(){
 const uint32_t address=csr_read(VX_CSR_MSCRATCH);
 if(address!=0x80100040||vx_num_cores()!=4||vx_num_warps()!=4||vx_num_threads()!=4||!rounding_ok())return 1;
 Invocation c{};c.args=*reinterpret_cast<const VxnnHunetArgs*>(uintptr_t(address));
 const size_t nd=c.args.op==15?c.args.extent[VXH_STATS]/64:0;const auto*ds=c.args.op==15?reinterpret_cast<const HunetFusedDescriptorV1*>(uintptr_t(c.args.address[VXH_STATS])):nullptr;
 // Validate address span BEFORE reading any descriptor in that span. Driver
 // guarantees actual allocation/ref containment; this adds device-side bounds.
 if(c.args.op==15&&(c.args.extent[VXH_STATS]%64||c.args.address[VXH_STATS]<0x80800000u||uint64_t(c.args.address[VXH_STATS])+c.args.extent[VXH_STATS]>0xfff00000u||(c.args.address[VXH_STATS]&3)))return 1;
 if(vxnn_hunet_validate(&c.args,ds,nd,true,&c.shape))return 1;
 *error_word(vx_core_id())=0;main_error=0;fence();vx_barrier(0x80000000,4);uint32_t block=4;int rc=0;
 if(c.args.op==15){auto&s=c.fused;s.magic=VXL_FUSED_MAGIC;s.abi=1;s.bytes=128;s.op=15;s.descriptor_count=c.shape.descriptor_count;s.input_gpu=c.args.address[VXH_X];s.weights_gpu=c.args.address[VXH_W];s.bias_gpu=(c.args.flags&16)?c.args.address[VXH_BIAS]:0;s.output_gpu=c.args.address[VXH_Y];s.packed_b_gpu=c.args.address[VXH_WORK];s.packed_b_bytes=c.shape.packed_bytes;s.stage_descriptors_gpu=c.args.address[VXH_STATS];s.owner_trace_gpu=c.args.address[VXH_WORK]+c.shape.trace_offset;s.pack_grid_rows=c.shape.pack_rows;s.gemm_grid_tiles=c.shape.gemm_tiles;s.ci=c.args.ci;s.co=c.args.co;s.ih=c.args.ih;s.iw=c.args.iw;s.oh=c.args.oh;s.ow=c.args.ow;s.kernel_k=c.args.k;s.stride=c.args.stride;s.pad=c.args.pad;s.flags=c.args.flags&2;s.owner_trace_elements=c.shape.gemm_tiles;s.dtype=1;s.policy=1;
  uint32_t gemm_groups=s.gemm_grid_tiles;
#if VXNN_HUNET_TILE_N == 2
  if(vxnn_hunet_group_plan(ds,s.descriptor_count,2,&c.grouped))return 1;gemm_groups=c.grouped.total;
#endif
  rc=vx_spawn_threads(1,&s.pack_grid_rows,&block,hunet_controlled_pack,&c);if(rc)fail();fence();vx_barrier(0x80000000,4);fence();
  if(!failed()){rc=vx_spawn_threads(1,&gemm_groups,&block,hunet_controlled_gemm,&c);if(rc)fail();}
 }else{auto&j=c.scalar;j.magic=HUNET_MAGIC;j.abi=1;j.bytes=128;j.op=c.args.op;j.dtype=1;j.tasks=c.shape.tasks;j.ci=c.args.ci;j.co=c.args.co;j.ih=c.args.ih;j.iw=c.args.iw;j.oh=c.args.oh;j.ow=c.args.ow;j.groups=c.args.groups;j.n_div=c.args.n_div;j.epsilon_bits=c.args.epsilon_bits;j.nitems=j.op==HUNET_GN_STATS?j.ci*j.ih*j.iw:j.co*j.oh*j.ow;
  const auto&a=c.args;auto read=[&](unsigned i){return VxnnHunetRead{reinterpret_cast<const float*>(uintptr_t(a.address[i])),a.extent[i]/4};};c.buffers.x=read(VXH_X);if(j.op==HUNET_ADD||j.op==HUNET_CONCAT||j.op==HUNET_MUL_CHANNEL)c.buffers.w=read(VXH_W);if(j.op==HUNET_GN_APPLY){c.buffers.gamma=read(VXH_GAMMA);c.buffers.beta=read(VXH_BETA);c.buffers.stats=read(VXH_STATS);}c.buffers.y={reinterpret_cast<float*>(uintptr_t(a.address[VXH_Y])),a.extent[VXH_Y]/4};
  void(*body)(void*)=hunet_controlled_scalar;
  switch(j.op){
   case HUNET_GN_APPLY:body=hunet_controlled_gn_apply;break;
   case HUNET_ADD:body=hunet_controlled_add;break;
   case HUNET_MUL_CHANNEL:body=hunet_controlled_mul;break;
   case HUNET_COPY:body=hunet_controlled_copy;break;
   case HUNET_SLICE:body=hunet_controlled_slice;break;
   case HUNET_CONCAT:body=hunet_controlled_concat;break;
   case HUNET_SHIFT:body=hunet_controlled_shift;break;
   case HUNET_GN_STATS:body=hunet_controlled_gn_stats;break;
   case HUNET_GAP:body=hunet_controlled_gap;break;
  }
  uint32_t grid=vxnn_hunet_scalar_grid(&j);rc=vx_spawn_threads(1,&grid,&block,body,&c);if(rc)fail();
 }
 fence();main_error=*error_word(vx_core_id());return rc?rc:int(main_error);
}
