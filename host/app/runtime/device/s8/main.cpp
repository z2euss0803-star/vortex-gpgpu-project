#include "runtime.h"
#include "protocol.h"
#include "phase.h"
#include "admission.h"
extern int s8_run_negative(const vxl_s8_job_v1 *,uint32_t,uint32_t);
extern int s8_run_compat(const vxl_s8_job_v1 *,uint32_t,uint32_t);
__thread volatile uint32_t s8_tls_probe;
__thread const vxl_s8_job_v1 *s8_current_job;
int s8_run_job(const vxl_s8_job_v1 *j,uint32_t core,uint32_t warp,uint32_t lane){
 (void)lane;
 if(j->case_id==1)return 0;
 if(j->case_id>=2&&j->case_id<=5)return s8_run_compat(j,core,warp);
 if((j->case_id>>8)>=201)return s8_run_negative(j,core,warp);
 return s8_run_positive(j,core,warp);
}

void s8_group_finish(uint32_t error){
 const vxl_s8_job_v1 *j=s8_current_job;
 uint32_t core=vx_core_id(),warp=vx_warp_id(),lane=vx_thread_id();
 volatile vxl_s8_sync *sync=reinterpret_cast<volatile vxl_s8_sync *>(uintptr_t(j->control_base+64+core*64));
 /* Only failing threads write the same idempotent value. No zero store can
  * erase another thread's fault and no reserved/handshake word is reused. */
 if(error)sync->abort=1;
 s8_barrier_group();
 if(warp==0 && lane==0){
  uint32_t combined=sync->abort;
  volatile vxl_s8_core_done *done=reinterpret_cast<volatile vxl_s8_core_done *>(uintptr_t(j->identity_base+0x840+core*32));
  done->magic=VXL_S8_CORE_MAGIC;done->version=1;done->core=core;done->generation=j->generation;done->sequence=j->sequence;done->error=combined;done->harts=16;s8_fence();done->done=VXL_S8_DONE;
  if(core==0){
   volatile vxl_s8_global_done *g=reinterpret_cast<volatile vxl_s8_global_done *>(uintptr_t(j->identity_base+0x8c0));
   g->magic=VXL_S8_GLOBAL_MAGIC;g->version=1;g->bytes=64;g->generation=j->generation;g->sequence=j->sequence;g->cores=15;g->harts=64;
   for(uint32_t i=0;i<16;++i){g->run_id[i]=j->run_id[i];g->binding_tag[i]=j->binding_tag[i];}
   /* This is a publisher/key marker, not a claim that other cores arrived.
    * The driver independently requires all four core records and CRT exits. */
   s8_fence();g->done=VXL_S8_DONE;
  }
 }
 s8_barrier_group();
 if(s8_phase_mark(11)&&lane==0)sync->abort=1;
}

void s8_callback(void *argument){
 const vxl_s8_job_v1 *j=static_cast<const vxl_s8_job_v1 *>(argument);
 s8_current_job=j;
 uint32_t core=vx_core_id(),warp=vx_warp_id(),lane=vx_thread_id(),hart=vx_hart_id();
 uint32_t error=(hart!=core*16+warp*4+lane || blockIdx.x!=core || threadIdx.x!=warp*4+lane)?1u:0u;
 error|=static_cast<uint32_t>(s8_phase_open(j,core,warp));
 volatile uint32_t stack_probe=VXL_S8_STACK_TAG^hart;
 uint32_t tls_zero=s8_tls_probe==0;s8_tls_probe=VXL_S8_TLS_TAG^hart;
 volatile vxl_s8_identity_v1 *id=reinterpret_cast<volatile vxl_s8_identity_v1 *>(uintptr_t(j->identity_base+64+hart*32));
 id->core=core;id->warp=warp;id->lane=lane;id->hart=hart;id->sp=s8_sp();id->tp=s8_tp();id->stack_tag=stack_probe;id->tls_tag=tls_zero?s8_tls_probe:0xbad00000u;
 error|=tls_zero?0u:2u;
 if(j->core_mask&(1u<<core))error|=static_cast<uint32_t>(s8_run_job(j,core,warp,lane));
 s8_group_finish(error);
}

int main(){
 /* The original pointer is captured before vx_spawn_threads overwrites
  * MSCRATCH with per-core scheduler arguments. Each core owns its local copy. */
 const uint32_t original_args=csr_read(VX_CSR_MSCRATCH);
 if(original_args!=0x80100040u || vx_num_cores()!=4 || vx_num_warps()!=4 || vx_num_threads()!=4)return 1;
 vxl_s8_job_v1 local=*reinterpret_cast<const vxl_s8_job_v1 *>(uintptr_t(original_args));
 if(s8_device_admit(&local))return 2;
 uint32_t grid=4,block=16;
 int rc=vx_spawn_threads(1,&grid,&block,s8_callback,&local);
 s8_fence();
 const volatile vxl_s8_core_done *done=reinterpret_cast<const volatile vxl_s8_core_done *>(uintptr_t(local.identity_base+0x840+vx_core_id()*32));
 return rc?rc:static_cast<int>(done->error);
}
