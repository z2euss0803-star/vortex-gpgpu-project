#include "memory_fixture.h"
int main(void){
 struct fixture *f=calloc(1,sizeof(*f));struct vxl_hunet_begin_v1 b={0};vxl_u8 *input=malloc(196608);struct vxl_request r;struct vxl_hunet_job_evidence_v1 ev;unsigned i;vxl_u32 bad[32];
 init_fixture(f);seal_fixture(f);file_read("../hunet_original_tcu_20261008_v1/data/board_inputs/p1_001.f32",input,196608);b.magic=VXL_HUNET_BEGIN_MAGIC;b.version=1;b.bytes=128;b.run_id[0]=7;b.sample_kind=4;b.input_ptr=(uintptr_t)input;b.input_bytes=196608;vxl_hunet_hash(input,196608,b.input_sha256);assert(!vxl_hunet_begin(&f->c,&b,&f->c.io));
 r=req();r.handle=f->c.handles[0];r.handle2=f->c.handles[3];r.trust_id=6;r.deadline_ms=60000;
 for(i=0;i<32;i++){memcpy(bad,vxl_hunet_catalog_job(1),128);bad[i]^=1;assert(!vxl_hunet_stage_args(&f->c,bad,128));assert(vxl_engine_call(&f->e,123,VXL_SUBMIT,&r)!=0);assert(f->starts==0);}
 for(i=0;i<128;i++)assert(!f->ps[0x10040+i]);
 assert(!vxl_hunet_stage_args(&f->c,vxl_hunet_catalog_job(1),128));f->code[100]^=1;assert(vxl_engine_call(&f->e,123,VXL_SUBMIT,&r)==-EACCES);assert(f->starts==0);f->code[100]^=1;
 for(i=1;i<=1023;i++){
  assert(!vxl_hunet_stage_args(&f->c,vxl_hunet_catalog_job(i),128));assert(!vxl_engine_call(&f->e,123,VXL_SUBMIT,&r));
  f->done=1;assert(!vxl_engine_poll(&f->e));assert(f->e.job.state==VXL_COMPLETED);
  assert(!vxl_hunet_get_evidence(&f->c,r.job_id,&ev));assert(ev.sequence==i&&ev.software_state==VXL_COMPLETED);assert(vxl_engine_call(&f->e,123,VXL_REAP,&r)==-EBUSY);
  vxl_hunet_evidence_delivered(&f->c,&ev);assert(!vxl_engine_call(&f->e,123,VXL_REAP,&r));
  if(i==1)f->code[f->r.program[0].ro_bytes+32]^=1; /* device RW change, not an upload */
 }
 assert(f->starts==1023&&f->c.next_sequence==1024);puts("HUNET_REAL_ENGINE_1023_CONTROL_JOBS_MUTATION_NO_START_NOT_GPU");destroy_fixture(f);free(f);free(input);return 0;
}
