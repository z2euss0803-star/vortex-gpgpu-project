#include "ops.h"
#include "ranges.h"
#include <errno.h>
namespace {
bool gpu_span(uint32_t p,uint32_t n,uint32_t alignment){return n&&p>=0x80800000u&&p%alignment==0&&uint64_t(p)+n<=0xfff00000ULL;}
bool gpu_overlap(uint32_t a,uint32_t an,uint32_t b,uint32_t bn){return uint64_t(a)<uint64_t(b)+bn&&uint64_t(b)<uint64_t(a)+an;}
}
int vxnn_i16_validate_job(const vxnn_i16_job_v1*j){
 if(!j||j->version!=1||j->bytes!=128||!vxnn_device::matrix(j->m,j->n)||!(j->generation_low|j->generation_high)||!(j->sequence_low|j->sequence_high)||!(j->cookie_low|j->cookie_high))return -EINVAL;
 uint32_t tag=0;for(auto b:j->binding)tag|=b;if(!tag)return -EINVAL;
 for(auto r:j->reserved)if(r)return -EINVAL;
 uint32_t a_bytes,b_bytes,a_alignment,b_alignment;
 if(j->op==VXNN_MATMUL_I16){if(!j->k||j->k>64)return -EINVAL;a_bytes=j->m*j->k*2;b_bytes=j->k*j->n*2;a_alignment=2;b_alignment=2;}
 else if(j->op==VXNN_BIAS_I48){if(j->k)return -EINVAL;a_bytes=j->m*j->n*8;b_bytes=j->n*4;a_alignment=8;b_alignment=4;}
 // One sealed schema has three address relocations. ReLU's second READ ref
 // is exactly the first input alias, not an unchecked optional GPU pointer.
 else if(j->op==VXNN_RELU_I48){if(j->k||j->b_gpu!=j->a_gpu||j->b_bytes!=j->a_bytes)return -EINVAL;a_bytes=j->m*j->n*8;b_bytes=a_bytes;a_alignment=8;b_alignment=8;}
 else return -EINVAL;
 if(j->a_bytes!=a_bytes||j->b_bytes!=b_bytes||j->out_bytes!=j->m*j->n*8||!gpu_span(j->a_gpu,a_bytes,a_alignment)||!gpu_span(j->out_gpu,j->out_bytes,8)||gpu_overlap(j->a_gpu,a_bytes,j->out_gpu,j->out_bytes))return -EINVAL;
 if(b_bytes&&(!gpu_span(j->b_gpu,b_bytes,b_alignment)||gpu_overlap(j->b_gpu,b_bytes,j->out_gpu,j->out_bytes)))return -EINVAL;
 return 0;
}

#if defined(__riscv) && (__riscv_xlen==32)
#include <vx_intrinsics.h>
#include <vx_spawn.h>
static __thread int callback_error;
static void memory_fence(){__asm__ volatile("fence iorw, iorw":::"memory");}
static void callback(void*argument){
 const auto*j=static_cast<const vxnn_i16_job_v1*>(argument);
 const uint32_t core=vx_core_id(),warp=vx_warp_id(),lane=vx_thread_id();
 if(warp)return;
 // Frozen private CRT: +8 is the real main exit word, stride256 per core.
 // The bounded intra-warp error slot is separate, still inside the4KiB CRT.
 auto*abort=reinterpret_cast<volatile uint32_t*>(uintptr_t(0x10002000u+core*256+64));
 if(lane==0)*abort=0;memory_fence();vx_barrier(0,1);
 uint32_t fcsr;__asm__ volatile("csrr %0, fcsr":"=r"(fcsr));
 int rc=fcsr||vx_hart_id()!=core*16+lane||blockIdx.x!=core||threadIdx.x!=lane?-EINVAL:vxnn_i16_validate_job(j);
 if(!rc){
  const auto*a=reinterpret_cast<const void*>(uintptr_t(j->a_gpu));auto*out=reinterpret_cast<int64_t*>(uintptr_t(j->out_gpu));
  if(j->op==VXNN_MATMUL_I16){auto io=mosaic_native_s8_ops(core,warp,lane,65536);rc=vxnn_i16_matmul(static_cast<const int16_t*>(a),j->a_bytes/2,reinterpret_cast<const int16_t*>(uintptr_t(j->b_gpu)),j->b_bytes/2,j->m,j->n,j->k,out,j->out_bytes/8,&io);}
  else if(lane==0&&j->op==VXNN_BIAS_I48)rc=vxnn_i48_bias(static_cast<const int64_t*>(a),j->a_bytes/8,reinterpret_cast<const int32_t*>(uintptr_t(j->b_gpu)),j->b_bytes/4,j->m,j->n,out,j->out_bytes/8,core);
  else if(lane==0)rc=vxnn_i48_relu(static_cast<const int64_t*>(a),j->a_bytes/8,out,j->out_bytes/8,core);
 }
 __asm__ volatile("csrr %0, fcsr":"=r"(fcsr));if(fcsr)rc=-EDOM;
 if(rc)*abort=1;memory_fence();vx_barrier(0,1);callback_error=*abort?1:0;memory_fence();vx_barrier(0,1);
}
int main(){
 uint32_t address=csr_read(VX_CSR_MSCRATCH);
 if(vx_num_cores()!=4||vx_num_warps()!=4||vx_num_threads()!=4||address<0x80100000u||address>0x8010ff80u||(address&63))return 1;
 // Copy before vx_spawn repurposes MSCRATCH. No CPU VA is a GPU address.
 vxnn_i16_job_v1 job=*reinterpret_cast<const vxnn_i16_job_v1*>(uintptr_t(address));
 if(vxnn_i16_validate_job(&job))return 1;
 uint32_t grid=4,block=4;callback_error=0;
 int rc=vx_spawn_threads(1,&grid,&block,callback,&job);memory_fence();return rc?rc:callback_error;
}
#endif
