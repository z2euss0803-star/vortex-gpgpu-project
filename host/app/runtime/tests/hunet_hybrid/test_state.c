#include <vxl/hunet_hybrid.h>
#include "../hunet/memory_fixture.h"
static struct vxl_hybrid_release hybrid_release(struct fixture *f){
 struct vxl_hybrid_release r={0};r.binding.v1=f->r.binding;r.binding.v1.version=2;r.binding.v1.bytes=384;r.binding.v1.program_id=8;r.binding.v1.features=7;
 memset(r.binding.cpu_binary_sha256,1,32);memset(r.binding.cpu_contract_sha256,2,32);memset(r.binding.cpu_stages_sha256,3,32);vxl_hunet_hash(&r.binding,384,r.binding_sha);return r;
}
static void hseal(struct fixture *f,struct vxl_hybrid_release *r){
 struct vxl_hunet_seal_v1 s={.magic=VXL_HYBRID_SEAL_MAGIC,.version=1,.bytes=128};
 assert(!vxl_hybrid_init(&f->c,r));
 s.code=reserve_fixture(f,VXL_CODE,VXL_HUNET_CODE,8388608);s.weights=reserve_fixture(f,VXL_WEIGHTS,VXL_HUNET_WEIGHTS,67108864);s.workspace=reserve_fixture(f,VXL_WORKSPACE,VXL_HUNET_WS,VXL_HUNET_WS_BYTES);s.args=reserve_fixture(f,VXL_ARGS,0,128);memcpy(s.binding_sha256,r->binding_sha,32);
 assert(!vxl_hybrid_seal(&f->c,&s));assert(f->c.program_id==8&&f->c.hybrid.enabled);
}
int main(void){
 struct fixture f;init_fixture(&f);seal_fixture(&f);assert(f.c.program_id==6&&!f.c.hybrid.enabled);destroy_fixture(&f);
 init_fixture(&f);struct vxl_hybrid_release r=hybrid_release(&f);hseal(&f,&r);
 f.e.snapshot.ready=f.e.snapshot.idle=1;f.c.active=1;f.c.next_sequence=1;vxl_hybrid_start_roi(&f.c);struct vxl_hunet_job_v1 j;
 for(unsigned seq=1;seq<=1023;seq++){
  memcpy(&j,vxl_hunet_catalog_job(seq),128);assert(f.c.next_sequence==seq);
  if(j.op==6){
   assert(vxl_hybrid_check_gpu(&f.c,&j)==-EACCES);assert(!vxl_hybrid_enter_cpu(&f.c));
   assert(vxl_hybrid_check_gpu(&f.c,&j)==-EBUSY);f.c.hybrid.phase=VXL_HYBRID_CPU_COMMITTED;
   assert(!vxl_hybrid_cpu_delivered(&f.c));assert(vxl_hybrid_cpu_delivered(&f.c)==-ESTALE);
  }else{
   assert(!vxl_hybrid_check_gpu(&f.c,&j));j.y+=4;assert(vxl_hybrid_check_gpu(&f.c,&j)==-EACCES);j.y-=4;
   j.sequence++;assert(vxl_hybrid_check_gpu(&f.c,&j)==-EINVAL);j.sequence--;
   assert(!vxl_hybrid_gpu_completed(&f.c));
  }
 }
 assert(f.c.next_sequence==1024&&f.c.hybrid.cpu_jobs==13&&f.c.hybrid.gpu_jobs==1010&&f.c.hybrid.logical_jobs==1023);
 /* A pending CPU phase must prevent real generic operations before callbacks. */
 f.c.next_sequence=296;f.c.hybrid.phase=VXL_HYBRID_GPU_READY;assert(!vxl_hybrid_enter_cpu(&f.c));
 struct vxl_request q=req();q.handle=f.c.handles[0];q.handle2=f.c.handles[3];q.trust_id=8;q.deadline_ms=60000;
 assert(vxl_engine_call(&f.e,123,VXL_SUBMIT,&q)==-EBUSY);assert(f.starts==0);
 assert(vxl_engine_call(&f.e,123,VXL_FREE,&q)==-EBUSY);
 assert(vxl_engine_call(&f.e,123,VXL_HUNET_END_ROI,&q)==-EBUSY);
 q.handle=f.c.handles[2];q.user_ptr=(uintptr_t)&j;q.length=4;assert(vxl_engine_call(&f.e,123,VXL_COPY_TO,&q)==-EBUSY);
 f.clock_ns=f.c.hybrid.deadline_ns;vxl_engine_poll(&f.e);
 assert(f.e.quarantined&&f.e.job.pinned&&f.c.hybrid.phase==VXL_HYBRID_ABORTED&&f.c.next_sequence==296);
 vxl_engine_close(&f.e,123);assert(f.e.owner==123&&f.e.quarantined);
 destroy_fixture(&f);
 init_fixture(&f);r=hybrid_release(&f);hseal(&f,&r);f.e.snapshot.ready=f.e.snapshot.idle=1;f.c.active=1;f.c.next_sequence=296;vxl_hybrid_start_roi(&f.c);assert(!vxl_hybrid_enter_cpu(&f.c));vxl_engine_close(&f.e,123);assert(f.e.quarantined&&f.e.job.pinned);destroy_fixture(&f);
 puts("HOST_HYBRID_1010_GPU_13_CPU_ROLES_TIMEOUT_CLOSE_PASS_NOT_BOARD");return 0;
}
