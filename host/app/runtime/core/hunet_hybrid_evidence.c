#include <vxl/engine.h>
#include <vxl/hunet.h>
int vxl_hybrid_get_cpu_evidence(struct vxl_hunet_ctx *c,vxl_u64 token,struct vxl_hybrid_cpu_evidence_v1 *out){
 struct vxl_hybrid_ctx *h;
 if(!c||!out)return -EFAULT;
 h=&c->hybrid;
 if(!h->enabled||!c->engine->opened||h->owner!=c->engine->owner||h->generation!=c->engine->generation||!token||token!=h->evidence.token)return -ESTALE;
 if(!h->evidence.magic)return -ENOENT;
 *out=h->evidence;
 /* Delivery is a claim on a successful two-copy ioctl return. State advances
    only in delivery_finish, after that exact payload and envelope were copied. */
 if(h->commit_receipt.token&&out->result==0)out->check_flags|=VXL_HYBRID_CHECK_DELIVERY;
 return 0;
}
int vxl_hybrid_end(struct vxl_hunet_ctx *c,struct vxl_hybrid_end_v1 *out){
 struct vxl_hybrid_ctx *h;struct vxl_hunet_end_v1 old;int rc;
 if(!c||!out)return -EFAULT;
 h=&c->hybrid;
 if(!h->enabled||!vxl_hybrid_mode(c)||!c->active||h->owner!=c->engine->owner||h->generation!=c->engine->generation)return -EACCES;
 if(h->logical_jobs!=1023||h->gpu_jobs!=vxl_hybrid_expected_gpu_jobs(c)||h->cpu_jobs!=13||h->phase!=VXL_HYBRID_GPU_READY||c->next_sequence!=1024)return -EBUSY;
 if(h->end_pending){*out=h->end_receipt;return 0;}
 rc=vxl_hybrid_idle(c);if(rc){vxl_hybrid_abort(c,rc,0);return rc;}
 if((vxl_u32)(c->engine->snapshot.jobs-h->hw_jobs_before)!=vxl_hybrid_expected_gpu_jobs(c)){vxl_hybrid_abort(c,-EIO,0);return -EIO;}
 rc=vxl_hunet_end(c,&old);if(rc)return rc;
 memset(out,0,sizeof(*out));out->magic=VXL_HYBRID_END_MAGIC;out->version=1;out->bytes=256;memcpy(out->run_id,c->run_id,16);out->roi=c->roi_index;out->generation=c->engine->generation;out->logical_jobs=1023;out->gpu_jobs=1010;out->cpu_jobs=13;out->stages=100;out->final_idle=old.final_idle;out->check_flags=old.check_flags;out->last_gpu_job_id=old.last_job_id;out->hw_jobs_before=h->hw_jobs_before;out->hw_jobs_after=c->engine->snapshot.jobs;
 out->gpu_jobs=vxl_hybrid_expected_gpu_jobs(c);
 memcpy(out->input_sha256,old.input_sha256,32);memcpy(out->logits_sha256,old.logits_sha256,32);memcpy(out->binding_sha256,c->program_id==10?c->opt.release->binding_sha:c->program_id==9?c->fused.release->binding_sha:h->release->binding_sha,32);h->end_receipt=*out;h->end_pending=1;return 0;
}
void vxl_hybrid_delivery_capture(struct vxl_hunet_ctx *c,vxl_u32 cmd,struct vxl_hybrid_delivery *d){
 int rc;memset(d,0,sizeof(*d));if(!c||!c->hybrid.enabled)return;
 if(cmd==VXL_HYBRID_GET_CPU_EVIDENCE){rc=vxl_hybrid_get_cpu_evidence(c,c->hybrid.evidence.token,&d->payload.cpu);if(rc)return;}
 else if(cmd==VXL_HYBRID_END&&c->hybrid.end_pending)d->payload.end=c->hybrid.end_receipt;
 else return;
 d->owner=c->engine->owner;d->generation=c->engine->generation;d->command=cmd;
}
void vxl_hybrid_delivery_finish(struct vxl_hunet_ctx *c,const struct vxl_hybrid_delivery *d){
 struct vxl_hybrid_ctx *h;struct vxl_hybrid_cpu_evidence_v1 expected;
 if(!c||!d||!d->command||!c->engine->opened||d->owner!=c->engine->owner||d->generation!=c->engine->generation)return;
 h=&c->hybrid;
 if(d->command==VXL_HYBRID_GET_CPU_EVIDENCE&&h->phase==VXL_HYBRID_CPU_COMMITTED){
  if(vxl_hybrid_get_cpu_evidence(c,d->payload.cpu.token,&expected)||memcmp(&expected,&d->payload.cpu,384))return;
  h->evidence.check_flags|=VXL_HYBRID_CHECK_DELIVERY;(void)vxl_hybrid_cpu_delivered(c);
 }else if(d->command==VXL_HYBRID_END&&h->end_pending&&!memcmp(&d->payload.end,&h->end_receipt,256)){
  c->active=0;c->end_pending=0;h->end_pending=0;
 }
}
