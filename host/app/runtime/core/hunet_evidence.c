#include <vxl/engine.h>
#include <vxl/hunet.h>
#include <vxl/submit_timing.h>
#include "../generated/hunet/programs.h"
#include "../generated/hunet/wmma_contract.h"
static int guard(struct vxl_hunet_ctx *c,vxl_u64 a,vxl_u32 n,int init){
 vxl_u8 b[64];vxl_u32 take,i;int rc;
 while(n){take=n>64?64:n;memset(b,0xa5,64);rc=vxl_hunet_io_bytes(c,a,b,take,init);if(rc)return rc;if(!init)for(i=0;i<take;i++)if(b[i]!=0xa5)return -EIO;a+=take;n-=take;}return 0;
}
static int crt_guards(struct vxl_hunet_ctx *c,int init){unsigned i;int rc=guard(c,0x10002000,64,init);if(rc)return rc;rc=guard(c,0x10002480,2944,init);if(rc)return rc;for(i=0;i<64;i++){rc=guard(c,0xffff0000ULL-(i+1)*8192ULL,64,init);if(rc)return rc;}return guard(c,0xffff0000ULL,64,init);}
static int measured_crt_guards(struct vxl_hunet_ctx *c,int init){
 struct vxl_submit_time *t=c->submit_time;vxl_u64 begin=0;int rc;
 if(t){begin=c->io.now_ns(c->io.context);t->guard_depth++;}
 rc=crt_guards(c,init);
 if(t){vxl_time_add(t,init?&t->guard_init_ns:&t->guard_check_ns,begin,c->io.now_ns(c->io.context));t->guard_depth--;}
 return rc;
}
int vxl_hunet_prepare(struct vxl_hunet_ctx *c,struct vxl_prepared *p,vxl_u32 id){
 struct vxl_hunet_job_v1 j;vxl_u8 sha[32];int rc;unsigned i,index;memcpy(&j,c->args_shadow,128);
 if(id==6){if(!c->sealed||!c->active||c->program_id!=6)return -EACCES;rc=vxl_hunet_validate_job(c,&j,c->next_sequence);if(rc)return rc;}
 else if(id==8){rc=vxl_hybrid_check_gpu(c,&j);if(rc)return rc;}
 else if(id==10){
  if(!c->sealed||!c->active||!c->hybrid.enabled||c->program_id!=10||!c->opt.release)return -EACCES;
  if(c->hybrid.owner!=c->engine->owner||c->hybrid.generation!=c->engine->generation)return -ESTALE;
  if(vxl_hybrid_busy(c))return -EBUSY;
  rc=vxl_opt_validate_job(c,&j,c->next_sequence,&c->opt.pending);if(rc)return rc;c->opt.acked=0;
  rc=vxl_opt_prepare_control(c);if(rc)return rc;
 }
 else if(id==9){
  if(!c->sealed||!c->active||!c->hybrid.enabled||c->program_id!=9)return -EACCES;
  if(c->hybrid.owner!=c->engine->owner||c->hybrid.generation!=c->engine->generation)return -ESTALE;
  if(vxl_hybrid_busy(c))return -EBUSY;
  rc=vxl_fused_validate_job(c,&j,c->next_sequence,&c->fused.pending);if(rc)return rc;
  c->fused.acked=0;
 }
 else if(id==7){
  if(c->sealed||c->active)return -EBUSY;
  if(!memcmp(&j,vxl_hunet_wmma_args0,128))index=0;else if(!memcmp(&j,vxl_hunet_wmma_args1,128))index=1;else return -EACCES;
  for(i=0;i<3;i++){rc=vxl_hunet_hash_gpu(c,vxl_hunet_wmma_regions[index][i].address,vxl_hunet_wmma_regions[index][i].bytes,sha);if(rc)return rc;if(memcmp(sha,vxl_hunet_wmma_regions[index][i].sha,32))return -EACCES;}
  c->program_id=7;
 }else return -EACCES;
 rc=vxl_hunet_check_program(c,!c->program_sealed);if(rc)return rc;c->program_sealed=1;
 rc=vxl_hunet_io_bytes(c,VXL_HUNET_ARGS,&j,128,1);if(rc)return rc;
 memset(p,0,sizeof(*p));p->program_id=id;p->code_gpu=VXL_HUNET_CODE;p->args_gpu=VXL_HUNET_ARGS;p->args_word_count=32;memcpy(p->args_words,&j,128);return 0;
}
int vxl_hunet_before_start(struct vxl_hunet_ctx *c,vxl_u64 id,vxl_u32 before){
 struct vxl_hunet_job_v1 j;vxl_u32 sentinel=0xffffffffu;unsigned i;vxl_u64 now;int rc;
 if(!c||!c->io.now_ns||!id)return -EINVAL;
 if(c->evidence.job_id&&!c->evidence_delivered)return -EBUSY;
 rc=measured_crt_guards(c,1);if(rc)return rc;
 for(i=0;i<4;i++){rc=vxl_hunet_io_bytes(c,0x10002088ULL+i*0x100,&sentinel,4,1);if(rc)return rc;}
 memcpy(&j,c->engine->prepared.args_words,128);now=c->io.now_ns(c->io.context);if(now>~(vxl_u64)0-60000000000ULL)return -EOVERFLOW;
 memset(&c->evidence,0,sizeof(c->evidence));c->evidence_delivered=0;c->evidence.magic=VXL_HUNET_EVIDENCE_MAGIC;c->evidence.version=1;c->evidence.bytes=256;memcpy(c->evidence.run_id,c->run_id,16);c->evidence.job_id=id;c->evidence.sequence=j.sequence;c->evidence.op=j.op;c->evidence.stage_id=j.stage_id;c->evidence.program_id=c->engine->prepared.program_id;c->evidence.generation=c->engine->generation;c->evidence.submit_ns=now;c->evidence.execution_deadline_ns=now+60000000000ULL;c->evidence.jobs_before=before;c->evidence.software_state=VXL_RUNNING;
 rc=vxl_hunet_digest_hash(c,&j,128,c->evidence.args_sha256);if(rc)return rc;
 if(c->program_id==10){c->evidence.sequence=c->opt.pending.first;c->evidence.op=c->opt.pending.op;c->evidence.stage_id=c->opt.pending.first_stage;}
 if(c->program_id==9){struct vxl_fused_meta m;rc=vxl_fused_decode_meta(&j,128,&m);if(rc)return rc;if(memcmp(&m,&c->fused.pending,sizeof(m)))return -ESTALE;c->evidence.sequence=m.first;c->evidence.op=m.op;c->evidence.stage_id=m.stage;}
 return 0;
}
int vxl_hunet_exit_snapshot(struct vxl_hunet_ctx *c,struct vxl_hunet_hw_snapshot *s){
 unsigned i;int rc;s->exit_valid=0;for(i=0;i<4;i++){rc=vxl_hunet_io_bytes(c,0x10002088ULL+i*0x100,&s->exitcode[i],4,0);if(rc)return rc;s->exit_valid|=1u<<i;}return 0;
}
int vxl_hunet_crt_snapshot(struct vxl_hunet_ctx *c,struct vxl_hunet_hw_snapshot *s){
 int rc=vxl_hunet_exit_snapshot(c,s);if(rc)return rc;
 rc=measured_crt_guards(c,0);s->crt_guard_flags=rc?0:3;s->crt_checked=1;return rc;
}
void vxl_hunet_record_snapshot(struct vxl_hunet_ctx *c,const struct vxl_hunet_hw_snapshot *s,vxl_u64 when){
 struct vxl_hunet_job_evidence_v1 *out=&c->evidence;const struct vxl_event *ev=&s->event;unsigned i;
 if(!out->job_id||ev->id!=out->job_id||out->generation!=c->engine->generation)return;
 out->status=ev->status;out->errors=ev->errors;out->pending_w=ev->pending_w;out->pending_r=ev->pending_r;out->jobs_after=ev->jobs;out->ready=ev->ready;out->watchdog=s->watchdog;out->elapsed_raw=s->elapsed_raw;out->cdma_status=s->cdma_status;out->valid_flags|=VXL_HUNET_VALID_HW_SNAPSHOT;
 for(i=0;i<4;i++)if(s->exit_valid&(1u<<i))out->exitcode[i]=s->exitcode[i];
 if(s->exit_valid==15)out->valid_flags|=VXL_HUNET_VALID_EXIT_SNAPSHOT;
 if(s->crt_checked)out->crt_guard_flags=s->crt_guard_flags;
 if(ev->status&2)out->done_ns=when;
}
int vxl_hunet_accept_done(struct vxl_hunet_ctx *c,const struct vxl_hunet_hw_snapshot *s,vxl_u64 done,struct vxl_hunet_job_evidence_v1 *out){
 unsigned i;int rc;const struct vxl_event *ev=&s->event;
 *out=c->evidence;out->done_ns=done;out->status=ev->status;out->errors=ev->errors;out->pending_w=ev->pending_w;out->pending_r=ev->pending_r;out->jobs_after=ev->jobs;out->ready=ev->ready;out->watchdog=s->watchdog;out->elapsed_raw=s->elapsed_raw;out->cdma_status=s->cdma_status;out->crt_guard_flags=s->crt_guard_flags;memcpy(out->exitcode,s->exitcode,16);
 if(out->generation!=c->engine->generation||ev->id!=out->job_id||!out->job_id)return -ESTALE;
 if(!(ev->status&2)||(ev->status&4)||ev->errors||ev->pending_w||ev->pending_r||!ev->ready||ev->jobs!=(vxl_u32)(out->jobs_before+1)||s->watchdog!=4000000000u||s->crt_guard_flags!=3)return -EIO;
 for(i=0;i<4;i++)if(s->exitcode[i])return -EIO;
 if(done<out->submit_ns||done>out->execution_deadline_ns)return -ETIMEDOUT;
 out->valid_flags=VXL_HUNET_VALID_CRT|VXL_HUNET_VALID_HW_SNAPSHOT|VXL_HUNET_VALID_EXIT_SNAPSHOT;
 if(c->program_id==9||c->program_id==10){struct vxl_submit_time *t=c->submit_time;vxl_u64 guard_begin=0;
  if((s->cdma_status&0x4770)||!(s->cdma_status&2))return -EIO;
  if(t){guard_begin=c->io.now_ns(c->io.context);t->guard_depth++;}
  rc=vxl_hunet_guards(c,0);
  if(t){vxl_time_add(t,&t->guard_check_ns,guard_begin,c->io.now_ns(c->io.context));t->guard_depth--;}
  if(rc)return rc;
  if(c->program_id==10&&c->opt.pending.owner_tiles){rc=vxl_opt_owner_trace(c,&c->opt.pending,out->owner_counts);if(rc)return rc;out->valid_flags|=VXL_HUNET_VALID_OWNER;}
  if(c->program_id==9&&c->fused.pending.op==15){rc=vxl_fused_owner_trace(c,&c->fused.pending,out->owner_counts);if(rc)return rc;out->valid_flags|=VXL_HUNET_VALID_OWNER;}}
 return 0;
}
int vxl_hunet_after_ack(struct vxl_hunet_ctx *c,vxl_u32 ready,vxl_u32 idle,vxl_u64 ack){
 int rc;if(!idle||!ready||ack<c->evidence.done_ns)return -EIO;
 c->evidence.ack_ns=ack;c->evidence.idle=idle;c->evidence.ready=ready;c->evidence.valid_flags|=VXL_HUNET_VALID_TIMING;c->evidence.software_state=VXL_COMPLETED;c->evidence.result=0;
 if(c->program_id==7){struct vxl_hunet_job_v1 j;memcpy(&j,c->engine->prepared.args_words,128);rc=vxl_hunet_owner_trace(c,j.tasks,c->evidence.owner_counts);if(rc)return rc;c->evidence.valid_flags|=VXL_HUNET_VALID_OWNER;}
 if(c->active&&c->program_id==6)c->next_sequence++;
 if(c->active&&c->program_id==8){rc=vxl_hybrid_gpu_completed(c);if(rc)return rc;}
 if(c->active&&c->program_id==9)c->fused.acked=1;
 if(c->active&&c->program_id==10)c->opt.acked=1;
 return 0;
}
int vxl_hunet_get_evidence(struct vxl_hunet_ctx *c,vxl_u64 id,struct vxl_hunet_job_evidence_v1 *out){if(!id||id!=c->evidence.job_id||c->evidence.generation!=c->engine->generation)return -ESTALE;*out=c->evidence;return 0;}
void vxl_hunet_evidence_delivered(struct vxl_hunet_ctx *c,const struct vxl_hunet_job_evidence_v1 *out){
 if(out&&out->job_id&&out->software_state!=VXL_RUNNING&&!memcmp(out,&c->evidence,256)&&!c->evidence_delivered){
  c->evidence_delivered=1;
  if(c->program_id==10&&out->software_state==VXL_COMPLETED){int rc=vxl_opt_gpu_completed(c,&c->opt.pending);if(rc)vxl_hybrid_abort(c,rc,0);}
  if(c->program_id==9&&out->software_state==VXL_COMPLETED){int rc=vxl_fused_gpu_completed(c,&c->fused.pending);if(rc)vxl_hybrid_abort(c,rc,0);}
 }
}
