#include <vxl/s8_trace.h>
#include <vxl/engine.h>
static int changed(struct vxl_s8_ctx *c) {
 struct vxl_s8_trace *t=c->trace;
 if(t->revision==~(vxl_u64)0){t->overflow=1;t->failed=1;return -EOVERFLOW;}
 ++t->revision;memset(t->exported,0,sizeof(t->exported));memset(t->grant,0,sizeof(t->grant));c->trace_delivered=0;return 0;
}
static int refused(struct vxl_s8_ctx *c) {
 c->trace->overflow=1;c->trace->failed=1;
 if(!c->error_class)c->error_class=VXL_S8_RESOURCE_REFUSED;
 changed(c);return -ENOSPC;
}
int vxl_s8_trace_append(struct vxl_s8_ctx *c,vxl_u32 table,const void *row,vxl_u32 bytes) {
 struct vxl_s8_trace *t;if(!c||!c->trace||!row)return -EINVAL;t=c->trace;
 if(table==0){if(bytes!=sizeof(*t->jobs))return -EINVAL;if(t->jobs_used==256)return refused(c);memcpy(&t->jobs[t->jobs_used++],row,bytes);}
 else if(table==1){if(bytes!=sizeof(*t->operations))return -EINVAL;if(t->operations_used==2048)return refused(c);memcpy(&t->operations[t->operations_used++],row,bytes);}
 else return -EINVAL;
 return changed(c);
}
static int observe(struct vxl_s8_ctx *c,vxl_u32 id,vxl_u64 begin,vxl_u64 end,vxl_u64 bytes,int result) {
 struct vxl_s8_trace *t=c->trace;struct vxl_s8_op_row op;
 if(!t || !t->jobs_used)return 0; /* loading is recorded by the user envelope */
 const struct vxl_s8_job_row *job=&t->jobs[t->jobs_used-1];
 op=(struct vxl_s8_op_row){.job_id=job->job_id,.begin_ns=begin,.end_ns=end,.bytes=bytes,.generation=job->args.generation,.sequence=job->args.sequence,.metric=id,.valid=end>=begin,.result=result};
 if(result)t->failed=1;
 return vxl_s8_trace_append(c,1,&op,sizeof(op));
}
int vxl_s8_trace_init(struct vxl_s8_ctx *c,struct vxl_s8_trace *t,struct vxl_s8_job_row *jobs,struct vxl_s8_op_row *ops,const vxl_u8 boot[16]) {
 vxl_u8 nz=0;if(!c||!c->engine||!t||!jobs||!ops||!boot||c->trace)return -EINVAL;
 for(unsigned i=0;i<16;++i)nz|=boot[i];
 if(!nz)return -EINVAL;
 memset(t,0,sizeof(*t));t->jobs=jobs;t->operations=ops;t->generation=c->engine->generation;t->revision=1;memcpy(t->boot,boot,16);c->trace=t;c->observe=observe;return 0;
}
int vxl_s8_trace_open_allowed(const struct vxl_s8_ctx *c) {
 const struct vxl_s8_trace *t;if(!c||!c->trace)return 0;t=c->trace;
 if(t->active||t->failed||t->overflow||t->abandoned)return -EBUSY;
 if(t->jobs_used||t->operations_used)for(unsigned i=0;i<3;++i)if(t->exported[i]!=t->revision)return -EBUSY;
 return 0;
}
void vxl_s8_trace_new_epoch(struct vxl_s8_ctx *c) {
 struct vxl_s8_trace *t;if(!c||!c->trace)return;t=c->trace;
 t->jobs_used=t->operations_used=t->active=t->failed=t->overflow=t->abandoned=0;t->generation=c->engine->generation;changed(c);
}
int vxl_s8_trace_begin(struct vxl_s8_ctx *c,const struct vxl_s8_job_v1 *j,vxl_u64 id) {
 struct vxl_s8_trace *t;struct vxl_s8_job_row *row;
 if(!c||!j||!id)return -EINVAL;
 if(!c->trace)return 0;
 t=c->trace;
 if(t->failed||t->overflow||t->abandoned||t->active)return -EBUSY;
 if(t->jobs_used && c->trace_delivered!=7)return -EBUSY;
 if(t->jobs_used==256 || t->operations_used==2048)return refused(c);
 row=&t->jobs[t->jobs_used++];memset(row,0,sizeof(*row));row->args=*j;row->job_id=id;row->state=VXL_READY;
 int rc=changed(c);if(rc)return rc;
 memset(&c->hw,0,sizeof(c->hw));vxl_s8_timing_reset(c);
 /* This row is a new submission, not the frozen READY row it replaces.
  * Activate it before the timing guard decides whether to accept event0. */
 t->active=1;vxl_s8_timing_event(c,VXL_S8_SUBMIT_ENTER,c->submit_enter_ns);return 0;
}
void vxl_s8_trace_sync(struct vxl_s8_ctx *c) {
 struct vxl_s8_trace *t;struct vxl_s8_job_row *old;vxl_u32 state;int result;
 if(!c||!c->trace||!c->trace->jobs_used)return;
 t=c->trace;old=&t->jobs[t->jobs_used-1];
 state=c->engine->job.state;result=c->engine->job.result;
 if(result||c->error_class||c->engine->quarantined||c->times.invalid)t->failed=1;
 t->active=state==VXL_RUNNING;
 if(old->state!=state||old->result!=result||old->error_class!=c->error_class||memcmp(&old->hw,&c->hw,sizeof(c->hw))||memcmp(&old->timing,&c->times,sizeof(c->times))){old->hw=c->hw;old->timing=c->times;old->state=state;old->result=result;old->error_class=c->error_class;changed(c);}
}
void vxl_s8_trace_close(struct vxl_s8_ctx *c) {
 if(!c||!c->trace)return;
 vxl_s8_trace_sync(c);
 if(vxl_s8_trace_open_allowed(c)){c->trace->abandoned=1;c->trace->failed=1;changed(c);}
}
int vxl_s8_reader_begin(struct vxl_s8_ctx *c,vxl_u32 file,vxl_u64 bytes,struct vxl_s8_reader *r) {
 struct vxl_s8_trace *t;if(!c||!c->trace||file>2||!bytes||!r)return -EINVAL;t=c->trace;
 if(t->serial==~(vxl_u64)0)return refused(c);
 memset(r,0,sizeof(*r));r->origin=t;r->revision=t->revision;r->generation=t->generation;r->serial=++t->serial;r->file=file;r->bytes=bytes;return 0;
}
int vxl_s8_reader_advance(struct vxl_s8_ctx *c,struct vxl_s8_reader *r,vxl_u64 offset,vxl_u64 bytes,int ok) {
 if(!c||!c->trace||!r)return -EINVAL;
 if(r->origin!=c->trace||r->revision!=c->trace->revision||r->generation!=c->trace->generation){r->broken=1;return -ESTALE;}
 if(!ok){r->broken=1;return -EFAULT;}
 if(r->broken||r->finished||offset!=r->delivered||offset>r->bytes||bytes>r->bytes-offset){r->broken=1;return -EINVAL;}
 r->delivered+=bytes;return 0;
}
int vxl_s8_reader_finish(struct vxl_s8_ctx *c,struct vxl_s8_reader *r) {
 struct vxl_s8_trace *t;if(!c||!c->trace||!r)return -EINVAL;t=c->trace;
 if(r->origin!=t||r->file>2||!r->serial||r->broken||r->finished||r->revision!=t->revision||r->generation!=t->generation||r->delivered!=r->bytes||t->active)return -EBUSY;
 t->grant[r->file]=r->serial;t->grant_bytes[r->file]=r->bytes;r->finished=1;
 return vxl_s8_export_complete(c,r->file,r->delivered);
}
int vxl_s8_export_complete(struct vxl_s8_ctx *c,vxl_u32 file,vxl_u64 delivered) {
 struct vxl_s8_trace *t;if(!c||!c->trace||file>2)return -EINVAL;t=c->trace;
 if(!t->grant[file]||t->grant_bytes[file]!=delivered||t->active)return -EBUSY;
 t->grant[file]=0;t->exported[file]=t->revision;c->trace_delivered|=1u<<file;return 0;
}
static int text(vxl_s8_emit_fn f,void *p,const char *s){return f(p,s,strlen(s));}
static int number(vxl_s8_emit_fn f,void *p,vxl_u64 n,const char *suffix) {
 char b[32];unsigned i=31;b[i]=0;do{b[--i]='0'+n%10;n/=10;}while(n);
 int rc=text(f,p,b+i);return rc?rc:text(f,p,suffix);
}
static int hex(vxl_s8_emit_fn f,void *p,const vxl_u8 *b,unsigned n,const char *suffix) {
 static const char digits[]="0123456789abcdef";char out[2];int rc;
 for(unsigned i=0;i<n;++i){out[0]=digits[b[i]>>4];out[1]=digits[b[i]&15];rc=f(p,out,2);if(rc)return rc;}return text(f,p,suffix);
}
int vxl_s8_trace_emit(const struct vxl_s8_trace *t,const struct vxl_s8_program *program,vxl_u32 file,vxl_s8_emit_fn emit,void *p) {
 int rc;if(!t||!emit||file>2||t->jobs_used>256||t->operations_used>2048)return -EINVAL;
 if(file==0)return program?emit(p,(const char *)&program->binding,512):-ENODEV;
#define TXT(s) do{rc=text(emit,p,s);if(rc)return rc;}while(0)
#define NUM(n,s) do{rc=number(emit,p,(vxl_u64)(n),s);if(rc)return rc;}while(0)
#define HEX(b,n,s) do{rc=hex(emit,p,b,n,s);if(rc)return rc;}while(0)
 TXT("#VXL_S8_TRACE_V1 file=");NUM(file," jobs_capacity=256 operations_capacity=2048 jobs_used=");NUM(t->jobs_used," operations_used=");NUM(t->operations_used," overflow=");NUM(t->overflow," active=");NUM(t->active," failed=");NUM(t->failed," abandoned=");NUM(t->abandoned," generation=");NUM(t->generation," job=");NUM(t->jobs_used?t->jobs[t->jobs_used-1].job_id:0," sequence=");NUM(t->jobs_used?t->jobs[t->jobs_used-1].args.sequence:0," revision=");NUM(t->revision," boot=");HEX(t->boot,16,"\n");
 if(file==1){
  TXT("job_id,generation,sequence,case_id,state,result,error_class,elapsed_raw,elapsed_u64,elapsed_address,elapsed_valid,pre_ack,watchdog,status,errors,pending_w,pending_r,jobs,pre_ack_ready,pre_ack_idle,ack_ready,ack_idle,tex_pending,exit_valid,exit0,exit1,exit2,exit3,guard_valid,immutable_valid,input_valid,ack_valid,identity_valid,metadata_valid,metadata_count,crt_guards,run_id,binding_tag,clock,valid_events,timing_invalid,jobs_before,cdma_status");
  for(unsigned i=0;i<VXL_S8_TIME_EVENTS;++i){TXT(",event");NUM(i,"_first_ns,event");NUM(i,"_last_ns,event");NUM(i,"_calls");}
  for(unsigned i=0;i<VXL_S8_METRICS;++i){TXT(",metric");NUM(i,"_ns,metric");NUM(i,"_calls,metric");NUM(i,"_bytes,metric");NUM(i,"_first_ns,metric");NUM(i,"_last_begin_ns,metric");NUM(i,"_last_ns,metric");NUM(i,"_valid,metric");NUM(i,"_overflow");}TXT("\n");
  for(unsigned i=0;i<t->jobs_used;++i){
   const struct vxl_s8_job_row *j=&t->jobs[i];const struct vxl_s8_hw_snapshot *s=&j->hw;
   NUM(j->job_id,",");NUM(j->args.generation,",");NUM(j->args.sequence,",");NUM(j->args.case_id,",");NUM(j->state,",");if(j->result<0){TXT("-");NUM(-(vxl_u64)j->result,",");}else NUM(j->result,",");NUM(j->error_class,",");
   NUM(s->elapsed_raw,",");NUM(s->elapsed_raw,",");NUM(s->elapsed_address,",");NUM(s->elapsed_valid,",");NUM(s->pre_ack,",");NUM(s->watchdog,",");NUM(s->event.status,",");NUM(s->event.errors,",");NUM(s->event.pending_w,",");NUM(s->event.pending_r,",");NUM(s->event.jobs,",");NUM(s->event.ready,",");NUM(s->event.idle,",");NUM(s->ack_ready,",");NUM(s->ack_idle,",");NUM(s->tex_pending,",");NUM(s->exit_valid,",");
   for(unsigned k=0;k<4;++k)NUM(s->exitcode[k],",");
   NUM(s->guard_valid,",");NUM(s->immutable_valid,",");NUM(s->input_valid,",");NUM(s->ack_valid,",");NUM(s->identity_valid,",");NUM(s->metadata_valid,",");NUM(s->metadata_count,",");HEX(s->crt_guards,192,",");HEX(j->args.run_id,16,",");HEX(j->args.binding_tag,16,",");NUM(j->timing.clock,",");NUM(j->timing.valid_events,",");NUM(j->timing.invalid,",");NUM(s->jobs_before,",");NUM(s->cdma_status,"");
   for(unsigned k=0;k<VXL_S8_TIME_EVENTS;++k){TXT(",");NUM(j->timing.first[k],",");NUM(j->timing.last[k],",");NUM(j->timing.event_calls[k],"");}
   for(unsigned k=0;k<VXL_S8_METRICS;++k){const struct vxl_s8_metric *m=&j->timing.metrics[k];TXT(",");NUM(m->ns,",");NUM(m->calls,",");NUM(m->bytes,",");NUM(m->first_ns,",");NUM(m->last_begin_ns,",");NUM(m->last_ns,",");NUM(m->valid,",");NUM(m->overflow,"");}TXT("\n");
  }
 }else{
  TXT("job_id,generation,sequence,metric,begin_ns,end_ns,bytes,valid,result\n");
  for(unsigned i=0;i<t->operations_used;++i){const struct vxl_s8_op_row *o=&t->operations[i];NUM(o->job_id,",");NUM(o->generation,",");NUM(o->sequence,",");NUM(o->metric,",");NUM(o->begin_ns,",");NUM(o->end_ns,",");NUM(o->bytes,",");NUM(o->valid,",");if(o->result<0){TXT("-");NUM(-(vxl_u64)o->result,"\n");}else NUM(o->result,"\n");}
 }
#undef TXT
#undef NUM
#undef HEX
 return 0;
}
