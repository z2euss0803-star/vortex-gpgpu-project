#include "state_fixture.h"
#include <vxl/s8_trace.h>
#include <stdio.h>
static int count_bytes(void *p,const char *s,size_t n){vxl_u64 *count=p;(void)s;*count+=n;return 0;}
static int file_write(void *p,const char *s,size_t n){return fwrite(s,1,n,p)==n?0:-EIO;}
static void submit_enter_survives_ready_transition(void) {
 /* Actual trace/timing production functions, with no GPU/RTL execution.
  * Both the first READY->prepare and a subsequent frozen READY row matter. */
 struct vxl_engine engine={0};struct vxl_s8_ctx c={0};struct vxl_s8_trace t={0};
 struct vxl_s8_job_row *jobs=calloc(256,sizeof(*jobs));struct vxl_s8_op_row *ops=calloc(2048,sizeof(*ops));
 vxl_u8 boot[16]={1};struct vxl_s8_job_v1 args={.generation=2,.sequence=1};assert(jobs&&ops);
 engine.generation=2;engine.job.state=VXL_READY;c.engine=&engine;
 assert(!vxl_s8_trace_init(&c,&t,jobs,ops,boot));
 c.submit_enter_ns=111;assert(!vxl_s8_timing_event(&c,VXL_S8_SUBMIT_ENTER,111));
 assert(!vxl_s8_trace_begin(&c,&args,1));
 assert(c.times.valid_events&(1ULL<<VXL_S8_SUBMIT_ENTER));
 assert(c.times.event_calls[VXL_S8_SUBMIT_ENTER]==1);
 assert(c.times.first[VXL_S8_SUBMIT_ENTER]==111&&c.times.last[VXL_S8_SUBMIT_ENTER]==111);
 vxl_s8_trace_sync(&c);assert(!t.active);
 struct vxl_s8_job_row old=jobs[0];vxl_u64 revision=t.revision;
 assert(!vxl_s8_timing_event(&c,VXL_S8_SUBMIT_ENTER,999));
 assert(!vxl_s8_timing_add(&c,VXL_S8_MUTEX_WAIT,900,999,0));
 assert(c.times.first[VXL_S8_SUBMIT_ENTER]==111&&c.times.event_calls[VXL_S8_SUBMIT_ENTER]==1);
 assert(!c.times.metrics[VXL_S8_MUTEX_WAIT].valid&&t.revision==revision);
 c.trace_delivered=7;c.submit_enter_ns=222;args.sequence=2;
 assert(!vxl_s8_trace_begin(&c,&args,2));
 assert(!memcmp(&old,&jobs[0],sizeof(old)));
 assert(c.times.valid_events==(1ULL<<VXL_S8_SUBMIT_ENTER));
 assert(c.times.event_calls[VXL_S8_SUBMIT_ENTER]==1&&c.times.first[VXL_S8_SUBMIT_ENTER]==222&&c.times.last[VXL_S8_SUBMIT_ENTER]==222);
 vxl_s8_trace_sync(&c);assert(jobs[1].timing.first[VXL_S8_SUBMIT_ENTER]==222);
 assert(!memcmp(&old,&jobs[0],sizeof(old)));
 free(ops);free(jobs);
 puts("S8_READY_NEW_SUBMIT_EVENT_RECORDED_AND_PREVIOUS_ROW_FROZEN_PASS SOFTWARE_ONLY");
}
static void export_one(struct vxl_s8_ctx *c,unsigned file) {
 struct vxl_s8_reader reader;vxl_u64 bytes=0;
 assert(!vxl_s8_trace_emit(c->trace,c->program,file,count_bytes,&bytes));
 assert(bytes && !vxl_s8_reader_begin(c,file,bytes,&reader));
 assert(vxl_s8_reader_advance(c,&reader,1,bytes-1,1)==-EINVAL); /* seek/hole */
 assert(vxl_s8_reader_finish(c,&reader)<0);
 assert(!vxl_s8_reader_begin(c,file,bytes,&reader));
 assert(!vxl_s8_reader_advance(c,&reader,0,bytes/2,1));
 assert(vxl_s8_reader_finish(c,&reader)<0); /* not EOF */
 assert(!vxl_s8_reader_advance(c,&reader,bytes/2,bytes-bytes/2,1));
 assert(!vxl_s8_reader_finish(c,&reader));
}
int main(void){
 submit_enter_survives_ready_transition();
 struct state_fixture *f=calloc(1,sizeof(*f));struct vxl_s8_trace *t=calloc(1,sizeof(*t));
 struct vxl_s8_job_row *jobs=calloc(256,sizeof(*jobs));struct vxl_s8_op_row *ops=calloc(2048,sizeof(*ops));
 struct vxl_s8_program program={0};vxl_u8 boot[16]={9};assert(f&&t&&jobs&&ops);state_init(f);state_complete(f);
 f->c.program=&program;assert(!vxl_s8_trace_init(&f->c,t,jobs,ops,boot));
 struct vxl_s8_hw_snapshot saved_hw=f->c.hw;
 assert(!vxl_s8_trace_begin(&f->c,&f->c.job_args,1));
 f->c.hw=saved_hw;
 assert(vxl_s8_trace_open_allowed(&f->c)<0);vxl_s8_trace_sync(&f->c);assert(!t->active);
 assert(vxl_s8_export_complete(&f->c,0,512)<0); /* no verified reader */
 struct vxl_s8_reader bad;assert(!vxl_s8_reader_begin(&f->c,0,512,&bad));
 assert(vxl_s8_reader_advance(&f->c,&bad,0,512,0)==-EFAULT);assert(vxl_s8_reader_finish(&f->c,&bad)<0);
 export_one(&f->c,0);export_one(&f->c,1);assert(f->c.trace_delivered==3);
 struct vxl_s8_reader stale;assert(!vxl_s8_reader_begin(&f->c,2,100,&stale));
 struct vxl_s8_op_row op={.metric=VXL_S8_ACK_TIME,.begin_ns=1,.end_ns=2};
 assert(!vxl_s8_trace_append(&f->c,1,&op,sizeof(op)));assert(!f->c.trace_delivered);
 assert(vxl_s8_reader_advance(&f->c,&stale,0,100,1)==-ESTALE);
 export_one(&f->c,0);export_one(&f->c,1);export_one(&f->c,2);assert(f->c.trace_delivered==7);
 assert(!vxl_s8_trace_open_allowed(&f->c));
 {const char *names[]={"binding.bin","jobs.csv","operations.csv"};char name[128];
  for(unsigned i=0;i<3;++i){snprintf(name,sizeof(name),"build/s8/trace-%s",names[i]);FILE *out=fopen(name,"wb");assert(out);assert(!vxl_s8_trace_emit(t,&program,i,file_write,out));assert(!fclose(out));}}
 struct vxl_s8_job_row job=t->jobs[0];
 for(unsigned i=1;i<256;++i)assert(!vxl_s8_trace_append(&f->c,0,&job,sizeof(job)));
 assert(vxl_s8_trace_append(&f->c,0,&job,sizeof(job))==-ENOSPC);assert(t->jobs_used==256 && !memcmp(&t->jobs[0],&job,sizeof(job)));
 assert(t->overflow && vxl_s8_trace_open_allowed(&f->c)<0);
 /* Fresh TEST_ONLY epoch to test the independent operation capacity. */
 f->c.trace=NULL;f->c.error_class=0;assert(!vxl_s8_trace_init(&f->c,t,jobs,ops,boot));
 for(unsigned i=0;i<2048;++i)assert(!vxl_s8_trace_append(&f->c,1,&op,sizeof(op)));
 assert(vxl_s8_trace_append(&f->c,1,&op,sizeof(op))==-ENOSPC);assert(t->operations_used==2048 && !memcmp(&t->operations[0],&op,sizeof(op)));
 f->c.trace=NULL;f->c.error_class=0;assert(!vxl_s8_trace_init(&f->c,t,jobs,ops,boot));assert(!vxl_s8_trace_begin(&f->c,&f->c.job_args,1));vxl_s8_trace_sync(&f->c);
 vxl_u32 generation=f->m.engine.generation;vxl_s8_trace_close(&f->c);
 assert(t->abandoned && vxl_s8_trace_open_allowed(&f->c)<0 && f->m.engine.generation==generation);
 export_one(&f->c,0);export_one(&f->c,1);export_one(&f->c,2);assert(vxl_s8_trace_open_allowed(&f->c)<0);
 /* A faulted epoch remains refused by the actual engine admission path even
  * after readers have later retrieved all forensic bytes. */
 f->c.capture_pending=0;f->c.error_class=0;f->m.engine.owner=0;f->m.engine.opened=0;f->m.engine.job.state=VXL_READY;
 assert(vxl_engine_open(&f->m.engine,2)==-EBUSY && f->m.engine.generation==generation);
 /* TEST_ONLY reconstruction of a different healthy completed epoch. Only
  * this case may retire rows after all three exact snapshots were read. */
 f->c.trace=NULL;f->c.error_class=0;f->c.delivery_fault=0;
 assert(!vxl_s8_trace_init(&f->c,t,jobs,ops,boot));f->m.engine.owner=1;f->m.engine.opened=1;f->m.engine.job.state=VXL_COMPLETED;
 assert(!vxl_s8_trace_begin(&f->c,&job.args,1));f->c.hw=saved_hw;vxl_s8_trace_sync(&f->c);
 export_one(&f->c,0);export_one(&f->c,1);export_one(&f->c,2);
 vxl_engine_close(&f->m.engine,1);assert(!t->abandoned && !t->failed && !f->m.engine.owner);
 assert(!vxl_engine_open(&f->m.engine,2));assert(f->m.engine.generation==generation+1 && !t->jobs_used && !t->operations_used);
 state_destroy(f);free(ops);free(jobs);free(t);free(f);
 puts("S8_TRACE_CAPACITY_CONTIGUOUS_DELIVERY_STALE_AND_FAILED_EPOCH_PASS SOFTWARE_ONLY");return 0;
}
