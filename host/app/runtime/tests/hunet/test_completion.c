#include "memory_fixture.h"
int main(void){
 struct fixture *f=calloc(1,sizeof(*f));struct vxl_hunet_hw_snapshot s,bad;struct vxl_hunet_job_evidence_v1 out;struct vxl_request r;unsigned i;
 init_fixture(f);seal_fixture(f);f->c.active=1;f->c.next_sequence=1;memcpy(f->c.args_shadow,vxl_hunet_catalog_job(1),128);memcpy(f->e.prepared.args_words,f->c.args_shadow,128);f->e.prepared.program_id=6;
 f->e.job.id=1;f->e.next_job=1;f->e.job.state=VXL_RUNNING;f->e.job.pinned=1;f->e.job.jobs_before=0xffffffffu;f->e.job.deadline=60100;
 assert(!vxl_hunet_before_start(&f->c,1,0xffffffffu));memset(&s,0,sizeof(s));s.event.id=1;s.event.status=2;s.event.jobs=0;s.event.ready=1;s.watchdog=4000000000u;s.crt_guard_flags=3;
 assert(!vxl_hunet_get_evidence(&f->c,1,&out));vxl_hunet_evidence_delivered(&f->c,&out);assert(!f->c.evidence_delivered);
 /* Actual adapter interleaving: RUNNING payload copied, worker completes,
  * outer envelope copied. A stale payload cannot authorize REAP. */
 assert(!vxl_hunet_get_evidence(&f->c,1,&out));f->c.evidence.software_state=VXL_COMPLETED;
 vxl_hunet_evidence_delivered(&f->c,&out);assert(!f->c.evidence_delivered);f->c.evidence.software_state=VXL_RUNNING;
 for(i=0;i<10;i++){bad=s;switch(i){case 0:bad.event.status=0;break;case 1:bad.event.errors=1;break;case 2:bad.event.pending_w=1;break;case 3:bad.event.pending_r=1;break;case 4:bad.event.ready=0;break;case 5:bad.event.jobs=1;break;case 6:bad.exitcode[2]=0xffffffffu;break;case 7:bad.crt_guard_flags=1;break;case 8:bad.watchdog=0;break;case 9:bad.event.id=2;break;}assert(vxl_hunet_accept_done(&f->c,&bad,110000000,&out)!=0);}
 assert(!vxl_hunet_accept_done(&f->c,&s,110000000,&out));assert(out.jobs_before==0xffffffffu&&out.jobs_after==0);f->c.evidence=out;assert(!vxl_hunet_after_ack(&f->c,1,1,110001000));
 s.event.idle=1;s.event.kind=VXL_DONE;assert(!vxl_job_step(&f->e.job,&s.event));assert(f->e.job.state==VXL_COMPLETED);
 assert(!vxl_hunet_get_evidence(&f->c,1,&out));r=req();r.job_id=1;assert(vxl_engine_call(&f->e,123,VXL_REAP,&r)==-EBUSY);
 {struct vxl_request get=req();get.job_id=1;get.length=256;get.user_ptr=(uintptr_t)&out;f->output_fault=1;assert(vxl_engine_call(&f->e,123,VXL_HUNET_GET_EVIDENCE,&get)==-EFAULT);f->output_fault=0;assert(!vxl_engine_call(&f->e,123,VXL_HUNET_GET_EVIDENCE,&get));assert(out.job_id==1&&!f->c.evidence_delivered);}
 out.job_id=2;vxl_hunet_evidence_delivered(&f->c,&out);assert(vxl_engine_call(&f->e,123,VXL_REAP,&r)==-EBUSY);assert(!vxl_hunet_get_evidence(&f->c,1,&out));vxl_hunet_evidence_delivered(&f->c,&out);assert(!vxl_engine_call(&f->e,123,VXL_REAP,&r));
 assert(vxl_engine_open(&f->e,456)==-EBUSY);f->e.job.state=VXL_RUNNING;f->e.job.pinned=1;f->e.job.deadline=60100;f->clock_ns=f->c.evidence.execution_deadline_ns+1;vxl_engine_poll(&f->e);assert(f->e.quarantined&&f->e.job.pinned);vxl_engine_close(&f->e,123);assert(f->e.objects[0].handle);
 assert(!vxl_hunet_get_evidence(&f->c,1,&out));assert(out.software_state==VXL_QUARANTINED);
 puts("HUNET_DONE_MATRIX_WRAP_COPYOUT_RETENTION_DEADLINE_CLOSE_PASS_NOT_GPU");destroy_fixture(f);free(f);return 0;
}
