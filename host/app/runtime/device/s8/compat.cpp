#include "compat.h"
#include <errno.h>
unsigned s8_compat_bytes(uint32_t id){return id==2?256:id==3?128:id==4?20:id==5?16:0;}
int s8_execute_compat(const vxl_s8_job_v1 *j,uint32_t core,uint32_t warp,const uint8_t *a,uint32_t ab,const uint8_t *b,uint32_t bb,uint8_t *out,uint32_t bytes,const s8_compat_io *io){
 if(!j||!a||!b||!out||!io||!io->fence||io->participant>4||core>3||warp>3||bytes!=4096||j->magic!=VXL_S8_JOB_MAGIC||j->version!=1||j->bytes!=128||j->owner_warp>3)return -EINVAL;
 if(!(j->core_mask&(1u<<core))||warp!=j->owner_warp)return 0;
 unsigned id=j->case_id,n=s8_compat_bytes(id),want_a=id==2?256:id==3?128:16,want_b=id==2?256:id==3?64:id==4?40:32;
 if(!n||ab!=want_a||bb!=want_b||j->flags||j->k!=(id==3?8u:0u))return -EINVAL;
 int rc=0;
 if(id==2){auto *aa=reinterpret_cast<const uint32_t *>(a);auto *ba=reinterpret_cast<const uint32_t *>(b);auto *r=reinterpret_cast<volatile uint32_t *>(out+64);for(unsigned i=io->participant==4?0:io->participant;i<64;i+=io->participant==4?1:4)r[i]=aa[i]+ba[i];}
 else if(id==3){if(!io->wmma)return -ENODEV;rc=io->wmma(io->context,reinterpret_cast<const uint16_t *>(a),reinterpret_cast<const uint16_t *>(b),reinterpret_cast<volatile float *>(out+64));}
 else{if(!io->texture)return -ENODEV;rc=io->texture(io->context,reinterpret_cast<const uint32_t *>(b),n/4,reinterpret_cast<volatile uint32_t *>(out+64));}
 io->fence(io->context);
 if(io->participant==0||io->participant==4){
  vxl_s8_result_v1 m={};m.magic=VXL_S8_RESULT_MAGIC;m.version=1;m.bytes=256;m.case_id=id;m.sw_status=rc?6:0;m.generation=j->generation;m.sequence=j->sequence;m.core=core;m.warp=warp;m.owner_warp=warp;m.k=j->k;
  m.valid_bits=rc?0:S8_RAW_VALID;m.format=id==5?4:id;m.primary_elements=n/4;
  for(unsigned i=0;i<16;++i){m.run_id[i]=j->run_id[i];m.binding_tag[i]=j->binding_tag[i];}
  auto *src=reinterpret_cast<const uint8_t *>(&m);auto *dst=reinterpret_cast<volatile uint8_t *>(out+0x3c0);for(unsigned i=0;i<256;++i)dst[i]=src[i];io->fence(io->context);reinterpret_cast<volatile uint32_t *>(dst+120)[0]=VXL_S8_DONE;
 }
 io->fence(io->context);return rc;
}
#if defined(__riscv)
#include "runtime.h"
#include <vx_tensor.h>
namespace vt=vortex::tensor;
using WMMA=vt::wmma_context<4,vt::fp16,vt::fp32>;
static_assert(WMMA::tileM==8&&WMMA::tileN==4&&WMMA::tileK==8,"frozen legacy tile");
static int native_wmma(void *,const uint16_t *a,const uint16_t *b,volatile float *out){
 if((reinterpret_cast<uintptr_t>(a)|reinterpret_cast<uintptr_t>(b)|reinterpret_cast<uintptr_t>(out))&3u)return -EINVAL;
 a=static_cast<const uint16_t *>(__builtin_assume_aligned(a,4));
 b=static_cast<const uint16_t *>(__builtin_assume_aligned(b,4));
 WMMA::fragment_a aa;WMMA::fragment_b bb;WMMA::fragment_acc rr;
 WMMA::fill_fragment(rr,0.0f);WMMA::load_matrix_sync(aa,a,8);WMMA::load_matrix_sync(bb,b,4);WMMA::mma_sync(rr,aa,bb,rr);WMMA::store_matrix_sync(const_cast<float *>(out),rr,4);return 0;
}
/* Frozen custom-1 / funct7=0 / funct3=0. Descriptor is fixed by driver. */
static inline uint32_t vx_tex(uint32_t u,uint32_t v){uint32_t r;__asm__ volatile(".insn r 0x2b, 0, 0, %0, %1, %2":"=r"(r):"r"(u),"r"(v):"memory");return r;}
static int native_texture(void *,const uint32_t *uv,unsigned count,volatile uint32_t *out){
 vx_tmc(1);for(unsigned i=0;i<count;++i)out[i]=vx_tex(uv[i*2],uv[i*2+1]);s8_fence();vx_tmc(15);return 0;
}
static void native_fence(void *){s8_fence();}
int s8_run_compat(const vxl_s8_job_v1 *j,uint32_t core,uint32_t warp){
 uint32_t slot=core*4+j->owner_warp,id=j->case_id;
 const auto *a=reinterpret_cast<const uint8_t *>(uintptr_t(j->a_base+slot*j->input_slot_stride));const auto *b=reinterpret_cast<const uint8_t *>(uintptr_t(j->b_base+slot*j->input_slot_stride));auto *out=reinterpret_cast<uint8_t *>(uintptr_t(j->output_base+slot*j->output_stride));
 s8_compat_io io={nullptr,native_wmma,native_texture,native_fence,uint32_t(vx_thread_id())};return s8_execute_compat(j,core,warp,a,id==2?256:id==3?128:16,b,id==2?256:id==3?64:id==4?40:32,out,4096,&io);
}
#endif
