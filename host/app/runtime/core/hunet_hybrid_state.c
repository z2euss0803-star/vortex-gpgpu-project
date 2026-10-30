#include <vxl/engine.h>
#include <vxl/hunet.h>
#ifdef __KERNEL__
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
void *vxl_hybrid_alloc(size_t n){return kvzalloc(n,GFP_KERNEL);}
void vxl_hybrid_free(void *p){kvfree(p);}
#else
#include <stdlib.h>
void *vxl_hybrid_alloc(size_t n){return calloc(1,n);}
void vxl_hybrid_free(void *p){free(p);}
#endif
static int nz(const vxl_u8 *b,size_t n){size_t i;for(i=0;i<n;i++)if(b[i])return 1;return 0;}
int vxl_hybrid_mode(const struct vxl_hunet_ctx *c){
 return c&&((c->program_id==8&&c->hybrid.release)||(c->program_id==9&&c->fused.release)||(c->program_id==10&&c->opt.release));
}
vxl_u32 vxl_hybrid_expected_gpu_jobs(const struct vxl_hunet_ctx *c){return c&&c->program_id==10&&c->opt.release?c->opt.release->catalog->gpu_jobs:c&&c->program_id==9?103u:1010u;}
int vxl_hybrid_init(struct vxl_hunet_ctx *c,const struct vxl_hybrid_release *r){
 vxl_u8 sha[32];int rc;if(!c||!r||!c->release||c->sealed||c->active)return -EINVAL;
 rc=vxl_hybrid_payload_validate(VXL_HYBRID_GET_BINDING,&r->binding,384);if(rc)return rc;
 if(r->binding.v1.profile_id!=4||r->binding.v1.program_id!=8||r->binding.v1.features!=7||r->binding.v1.generation||r->binding.v1.state||memcmp(&r->binding.v1.profile_sha256,&c->release->binding.profile_sha256,224)||!nz(r->binding.cpu_binary_sha256,32)||!nz(r->binding.cpu_contract_sha256,32)||!nz(r->binding.cpu_stages_sha256,32))return -ESTALE;
 rc=vxl_hunet_digest_hash(c,&r->binding,384,sha);if(rc)return rc;if(memcmp(sha,r->binding_sha,32))return -ESTALE;
 c->hybrid.release=r;return 0;
}
int vxl_hybrid_seal(struct vxl_hunet_ctx *c,const struct vxl_hunet_seal_v1 *s){
 struct vxl_hunet_seal_v1 old;int rc;
 if(!c||!c->hybrid.release)return -EOPNOTSUPP;
 rc=vxl_hybrid_payload_validate(VXL_HYBRID_SEAL,s,128);if(rc)return rc;
 if(memcmp(s->binding_sha256,c->hybrid.release->binding_sha,32))return -ESTALE;
 old=*s;old.magic=VXL_HUNET_SEAL_MAGIC;memcpy(old.binding_sha256,c->release->binding_sha,32);
 rc=vxl_hunet_seal(c,&old);if(rc)return rc;
 c->program_id=8;c->hybrid.enabled=1;c->hybrid.owner=c->engine->owner;c->hybrid.generation=c->engine->generation;return 0;
}
int vxl_hybrid_busy(const struct vxl_hunet_ctx *c){return c&&c->hybrid.enabled&&c->hybrid.phase!=VXL_HYBRID_GPU_READY;}
void vxl_hybrid_start_roi(struct vxl_hunet_ctx *c){
 struct vxl_hybrid_ctx *h=&c->hybrid;
 h->phase=VXL_HYBRID_GPU_READY;h->logical_jobs=h->gpu_jobs=h->cpu_jobs=h->end_pending=0;
 h->hw_jobs_before=c->engine->snapshot.jobs;h->network_deadline_ns=c->io.now_ns(c->io.context)+(vxl_u64)VXL_HUNET_NETWORK_MS*1000000ULL;
 memset(&h->evidence,0,sizeof(h->evidence));memset(&h->end_receipt,0,sizeof(h->end_receipt));
}
static int owned(struct vxl_hunet_ctx *c){
 if(!c||!c->hybrid.enabled||!vxl_hybrid_mode(c)||!c->sealed||!c->active)return -EACCES;
 if(c->hybrid.owner!=c->engine->owner||c->hybrid.generation!=c->engine->generation||!c->engine->opened)return -ESTALE;
 if(c->engine->quarantined||c->hybrid.phase==VXL_HYBRID_ABORTED)return -EIO;
 return 0;
}
int vxl_hybrid_check_gpu(struct vxl_hunet_ctx *c,const struct vxl_hunet_job_v1 *j){
 int rc=owned(c);if(rc)return rc;
 if(vxl_hybrid_busy(c))return -EBUSY;
 if(!j)return -EINVAL;
 rc=vxl_hunet_validate_job(c,j,c->next_sequence);if(rc)return rc;
 return j->op==6?-EACCES:0;
}
int vxl_hybrid_gpu_completed(struct vxl_hunet_ctx *c){
 const vxl_u32 *j;int rc=owned(c);if(rc)return rc;
 j=vxl_hunet_catalog_job(c->next_sequence);
 if(vxl_hybrid_busy(c)||!j||j[3]==6)return -EINVAL;
 c->hybrid.gpu_jobs++;c->hybrid.logical_jobs++;c->next_sequence++;return 0;
}
int vxl_hybrid_enter_cpu(struct vxl_hunet_ctx *c){
 struct vxl_hybrid_ctx *h;struct vxl_hunet_job_v1 j;vxl_u64 now;int rc=owned(c);if(rc)return rc;h=&c->hybrid;
 if(vxl_hybrid_busy(c)||c->engine->job.state!=VXL_READY||c->engine->job.pinned)return -EBUSY;
 if(!vxl_hunet_catalog_job(c->next_sequence))return -EINVAL;
 memcpy(&j,vxl_hunet_catalog_job(c->next_sequence),128);
 if(j.op!=6)return -EACCES;
 if(c->evidence.job_id&&!c->evidence_delivered)return -EBUSY;
 if(c->engine->snapshot.errors||c->engine->snapshot.pending_r||c->engine->snapshot.pending_w||!c->engine->snapshot.idle||!c->engine->snapshot.ready)return -EIO;
 rc=vxl_hunet_validate_spans(&j,NULL);if(rc)return rc;
 if(j.tasks>393216||h->token_serial==~(vxl_u64)0)return -EOVERFLOW;
 now=c->io.now_ns(c->io.context);if(now>~(vxl_u64)0-60000000000ULL)return -EOVERFLOW;
 h->sequence=j.sequence;h->stage=j.stage_id;h->tensor_bytes=(vxl_u64)j.tasks*4;h->token=++h->token_serial;
 h->begin_ns=now;h->deadline_ns=now+60000000000ULL;h->phase=VXL_HYBRID_CPU_WAIT;h->input_ready=0;h->bytes_written=0;
 memset(&h->begin_receipt,0,sizeof(h->begin_receipt));memset(&h->commit_receipt,0,sizeof(h->commit_receipt));memset(&h->evidence,0,sizeof(h->evidence));return 0;
}
int vxl_hybrid_cpu_delivered(struct vxl_hunet_ctx *c){
 int rc=owned(c);if(rc)return rc;
 if(c->hybrid.phase!=VXL_HYBRID_CPU_COMMITTED)return -ESTALE;
 c->hybrid.cpu_jobs++;c->hybrid.logical_jobs++;c->next_sequence++;
 c->hybrid.phase=VXL_HYBRID_GPU_READY;vxl_hybrid_free(c->hybrid.cpu_input);vxl_hybrid_free(c->hybrid.cpu_output);c->hybrid.cpu_input=c->hybrid.cpu_output=NULL;return 0;
}
void vxl_hybrid_abort(struct vxl_hunet_ctx *c,int error,int partial_write){
 struct vxl_hybrid_ctx *h=&c->hybrid;(void)partial_write;
 h->phase=VXL_HYBRID_ABORTED;h->evidence.result=error;h->evidence.state=VXL_QUARANTINED;
 c->engine->quarantined=1;c->engine->job.state=VXL_QUARANTINED;c->engine->job.pinned=1;c->engine->job.result=error;
}
void vxl_hybrid_poll(struct vxl_hunet_ctx *c){
 vxl_u64 now;if(!c||!c->hybrid.enabled||!c->active||c->engine->quarantined)return;
 now=c->io.now_ns(c->io.context);
 if((c->hybrid.network_deadline_ns&&now>=c->hybrid.network_deadline_ns)||(vxl_hybrid_busy(c)&&now>=c->hybrid.deadline_ns))vxl_hybrid_abort(c,-ETIMEDOUT,0);
}
void vxl_hybrid_cleanup(struct vxl_hunet_ctx *c){
 const struct vxl_hybrid_release *r=c->hybrid.release;
 int (*check)(struct vxl_hunet_ctx *)=c->hybrid.hw_check;
 vxl_hybrid_free(c->hybrid.cpu_input);vxl_hybrid_free(c->hybrid.cpu_output);memset(&c->hybrid,0,sizeof(c->hybrid));c->hybrid.release=r;c->hybrid.hw_check=check;c->program_id=6;memset(&c->fused.pending,0,sizeof(c->fused.pending));c->fused.acked=0;memset(&c->opt.pending,0,sizeof(c->opt.pending));c->opt.acked=0;
}
int vxl_hybrid_idle(struct vxl_hunet_ctx *c){
 const struct vxl_event *s=&c->engine->snapshot;
 if(c->engine->job.state!=VXL_READY||c->engine->job.pinned||c->engine->quarantined||!s->idle||!s->ready||s->errors||s->pending_r||s->pending_w)return -EIO;
 return c->hybrid.hw_check?c->hybrid.hw_check(c):0;
}
int vxl_hybrid_transaction(struct vxl_hunet_ctx *c,const vxl_u8 run[16],vxl_u32 gen,vxl_u32 seq,vxl_u32 stage,vxl_u64 token){
 int rc;vxl_hybrid_poll(c);rc=owned(c);if(rc)return rc;
 if(gen!=c->engine->generation||seq!=c->hybrid.sequence||stage!=c->hybrid.stage||memcmp(run,c->run_id,16)||!token||token!=c->hybrid.token)return -ESTALE;
 return 0;
}
int vxl_hybrid_command(struct vxl_hunet_ctx *c,vxl_u32 cmd,struct vxl_request *r){
 struct vxl_hunet_seal_v1 s;struct vxl_hybrid_binding_v2 b;struct vxl_hybrid_begin_v1 begin;struct vxl_hybrid_commit_v1 commit;int rc;
 if(!c->hybrid.release&&!c->fused.release&&!c->opt.release)return -EOPNOTSUPP;
 if(!r->user_ptr||r->user_ptr>~(vxl_u64)0-r->length)return -EFAULT;
 if(cmd==VXL_HYBRID_GET_CPU_EVIDENCE){struct vxl_hybrid_cpu_evidence_v1 ev;if(r->length!=384)return -EINVAL;rc=vxl_hybrid_get_cpu_evidence(c,r->job_id,&ev);return rc?rc:c->engine->ops->write_user(c->engine->context,r->user_ptr,&ev,384);}
 if(cmd==VXL_HYBRID_END){struct vxl_hybrid_end_v1 end;if(r->length!=256)return -EINVAL;rc=vxl_hybrid_end(c,&end);return rc?rc:c->engine->ops->write_user(c->engine->context,r->user_ptr,&end,256);}
 if(cmd==VXL_HYBRID_GET_BINDING){if(c->opt.release){struct vxl_opt_binding_v4 opt;if(r->length!=512)return -EINVAL;opt=c->opt.release->binding;opt.v3.v2.v1.generation=c->engine->generation;opt.v3.v2.v1.state=c->engine->job.state;return c->engine->ops->write_user(c->engine->context,r->user_ptr,&opt,512);}if(r->length==512){struct vxl_fused_binding_v3 fused;if(!c->fused.release)return -EOPNOTSUPP;fused=c->fused.release->binding;fused.v2.v1.generation=c->engine->generation;fused.v2.v1.state=c->engine->job.state;return c->engine->ops->write_user(c->engine->context,r->user_ptr,&fused,512);}if(r->length!=384||!c->hybrid.release)return -EINVAL;b=c->hybrid.release->binding;b.v1.generation=c->engine->generation;b.v1.state=c->engine->job.state;return c->engine->ops->write_user(c->engine->context,r->user_ptr,&b,384);}
 if(cmd==VXL_HYBRID_SEAL){if(r->length!=128)return -EINVAL;rc=c->engine->ops->read_user(c->engine->context,&s,r->user_ptr,128);if(rc)return rc;if(c->opt.release)return vxl_opt_seal(c,&s);if(c->fused.release&&!memcmp(s.binding_sha256,c->fused.release->binding_sha,32))return vxl_fused_seal(c,&s);return vxl_hybrid_seal(c,&s);}
 if(cmd==VXL_HYBRID_CPU_BEGIN){if(r->length!=128)return -EINVAL;rc=c->engine->ops->read_user(c->engine->context,&begin,r->user_ptr,128);if(rc)return rc;rc=vxl_hybrid_begin_cpu(c,&begin);return rc?rc:c->engine->ops->write_user(c->engine->context,r->user_ptr,&begin,128);}
 if(cmd==VXL_HYBRID_CPU_COMMIT){if(r->length!=128)return -EINVAL;rc=c->engine->ops->read_user(c->engine->context,&commit,r->user_ptr,128);if(rc)return rc;rc=vxl_hybrid_commit_cpu(c,&commit);r->used=c->hybrid.bytes_written;return rc;}
 return -ENOTTY;
}
