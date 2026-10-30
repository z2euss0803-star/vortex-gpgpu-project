#include <utility>
#include <vx_spawn.h>
#include <vx_tensor.h>
#include "hunet_fused_ops.h"
namespace vt=vortex::tensor;
using WMMA=vt::wmma_context<4,vt::fp16,vt::fp32>;
static_assert(WMMA::tileM==8&&WMMA::tileN==4&&WMMA::tileK==8&&WMMA::fragment_acc::NR==8,"frozen HW4 WMMA");
static_assert(sizeof(HunetFusedStageV1)==128&&sizeof(HunetFusedDescriptorV1)==64,"GPU wire");
extern int hunet_v5_entry();

static const HunetFusedDescriptorV1* descriptors(const HunetFusedStageV1*s){
 return reinterpret_cast<const HunetFusedDescriptorV1*>(uintptr_t(s->stage_descriptors_gpu));
}
void hunet_fused_pack(HunetFusedStageV1* __UNIFORM__ s){
 const auto*ds=descriptors(s);
 for(uint32_t i=0;i<s->descriptor_count;++i){const auto&d=ds[i];
  if(blockIdx.x>=d.pack_grid_base&&blockIdx.x-d.pack_grid_base<d.kp){
   hunet_fused_pack_row(*s,d,reinterpret_cast<const float*>(uintptr_t(s->input_gpu)),
    reinterpret_cast<uint16_t*>(uintptr_t(s->packed_b_gpu+d.packed_b_offset)),
    blockIdx.x-d.pack_grid_base,threadIdx.x,blockDim.x);return;
  }
 }
}
void hunet_fused_gemm(HunetFusedStageV1* __UNIFORM__ s){
 const auto*ds=descriptors(s);
 for(uint32_t i=0;i<s->descriptor_count;++i){const auto&d=ds[i];
  const uint32_t tiles=(d.mp/8)*(d.np/4);
  if(blockIdx.x<d.gemm_grid_base||blockIdx.x-d.gemm_grid_base>=tiles)continue;
  uint32_t tile=blockIdx.x-d.gemm_grid_base,row=(tile/(d.np/4))*8,col=(tile%(d.np/4))*4;
  const auto*w=reinterpret_cast<const uint16_t*>(uintptr_t(s->weights_gpu));
  const auto*b=reinterpret_cast<const uint16_t*>(uintptr_t(s->packed_b_gpu+d.packed_b_offset));
  WMMA::fragment_a fw;WMMA::fragment_b fb;WMMA::fragment_acc acc;
  WMMA::fill_fragment(acc,0.0f);
  for(uint32_t k=0;k<d.kp;k+=8){
   WMMA::load_matrix_sync(fw,w+row*d.kp+k,d.kp);
   WMMA::load_matrix_sync(fb,b+k*d.np+col,d.np);
   WMMA::mma_sync(acc,fw,fb,acc);
  }
  auto*out=reinterpret_cast<float*>(uintptr_t(s->output_gpu));
  auto*bias=reinterpret_cast<const float*>(uintptr_t(s->bias_gpu));
  vt::detail::unroll_for<8>([&](auto reg){hunet_fused_store(d,out,bias,row,col,threadIdx.x,reg,acc.data[reg]);});
  if(threadIdx.x==0)reinterpret_cast<uint32_t*>(uintptr_t(s->owner_trace_gpu))[blockIdx.x]=vx_core_id()+1;
  return;
 }
}
int hunet_fused_run(const HunetFusedStageV1*s){
 // A local copy survives vx_spawn_threads' use of MSCRATCH on every core.
 HunetFusedStageV1 local=*s;
 if(local.magic!=VXL_FUSED_MAGIC||local.abi!=1||local.bytes!=128||local.op!=15||
    local.policy!=1||local.reserved||!local.descriptor_count||local.descriptor_count>32||
    !local.pack_grid_rows||!local.gemm_grid_tiles||local.owner_trace_elements!=local.gemm_grid_tiles||
    vx_num_cores()!=4||vx_num_warps()!=4||vx_num_threads()!=4)return 1;
 uint32_t block=4;
 int pack_rc=vx_spawn_threads(1,&local.pack_grid_rows,&block,(vx_kernel_func_cb)hunet_fused_pack,&local);
 // All four cores, including cores with no local work, join this phase.
 // FENCE orders/drains writes; the global barrier is distinct from local warp join.
 __asm__ volatile("fence iorw, iorw" ::: "memory");
 vx_barrier(0x80000000,4);
 __asm__ volatile("" ::: "memory");
 int gemm_rc=vx_spawn_threads(1,&local.gemm_grid_tiles,&block,(vx_kernel_func_cb)hunet_fused_gemm,&local);
 __asm__ volatile("fence iorw, iorw" ::: "memory");
 return pack_rc?pack_rc:gemm_rc;
}
int main(){
 // Capture the original argument before any spawn can overwrite MSCRATCH.
 const auto*s=reinterpret_cast<const HunetFusedStageV1*>(uintptr_t(csr_read(VX_CSR_MSCRATCH)));
 if(s->op!=VXL_FUSED_OP)return hunet_v5_entry();
 return hunet_fused_run(s);
}
