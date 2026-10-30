#include <vxl/engine.h>
#include <vxl/hunet.h>
int vxl_hybrid_snapshot_input(struct vxl_hunet_ctx *c){
 struct vxl_hybrid_ctx *h=&c->hybrid;struct vxl_hunet_job_v1 j;vxl_u64 start;int rc;vxl_u32 word;vxl_u64 i;
 if(h->input_ready)return 0;
 start=vxl_hunet_mgmt_now(c);rc=vxl_hybrid_idle(c);vxl_hunet_mgmt_mark(c,VXL_MGMT_IDLE,start,0);if(rc){vxl_hybrid_abort(c,rc,0);return rc;}
 start=vxl_hunet_mgmt_now(c);
 memcpy(&j,vxl_hunet_catalog_job(h->sequence),128);
 rc=vxl_hunet_validate_spans(&j,NULL);vxl_hunet_mgmt_mark(c,VXL_MGMT_VALIDATE,start,0);if(rc||j.op!=6||h->tensor_bytes!=(vxl_u64)j.tasks*4)return -EACCES;
 if(!h->cpu_input){start=vxl_hunet_mgmt_now(c);h->cpu_input=vxl_hybrid_alloc((size_t)h->tensor_bytes);vxl_hunet_mgmt_mark(c,VXL_MGMT_ALLOC,start,h->tensor_bytes);if(!h->cpu_input){vxl_hybrid_abort(c,-ENOMEM,0);return -ENOMEM;}}
 start=c->io.now_ns(c->io.context);rc=vxl_hunet_guards(c,0);h->evidence.guard_ns=c->io.now_ns(c->io.context)-start;
 vxl_hunet_mgmt_mark(c,VXL_MGMT_GUARD,start,0);
 if(rc){vxl_hybrid_abort(c,rc,0);return rc;}
 start=c->io.now_ns(c->io.context);rc=vxl_hunet_io_bytes(c,j.x,h->cpu_input,h->tensor_bytes,0);h->evidence.read_ns=c->io.now_ns(c->io.context)-start;
 vxl_hunet_mgmt_mark(c,VXL_MGMT_INPUT_READ,start,h->tensor_bytes);
 if(rc){vxl_hybrid_abort(c,rc,0);return rc;}
 start=vxl_hunet_mgmt_now(c);
 for(i=0;i<h->tensor_bytes;i+=4){memcpy(&word,h->cpu_input+i,4);if((word&0x7f800000)==0x7f800000){vxl_hunet_mgmt_mark(c,VXL_MGMT_FINITE,start,i+4);vxl_hybrid_abort(c,-EDOM,0);return -EDOM;}}
 vxl_hunet_mgmt_mark(c,VXL_MGMT_FINITE,start,h->tensor_bytes);start=vxl_hunet_mgmt_now(c);
 rc=vxl_hunet_digest_hash(c,h->cpu_input,h->tensor_bytes,h->begin_receipt.input_sha256);
 vxl_hunet_mgmt_mark(c,VXL_MGMT_HASH_INPUT,start,h->tensor_bytes);
 if(rc){vxl_hybrid_abort(c,rc,0);return rc;}
 memcpy(h->evidence.input_sha256,h->begin_receipt.input_sha256,32);h->input_ready=1;h->evidence.check_flags=VXL_HYBRID_CHECK_INPUT|VXL_HYBRID_CHECK_IDLE|VXL_HYBRID_CHECK_GUARD;return 0;
}
static int begin_prepare(struct vxl_hunet_ctx *c,struct vxl_hybrid_begin_v1 *p){
 struct vxl_hybrid_ctx *h;struct vxl_hunet_job_v1 j;int rc;
 h=&c->hybrid;
 rc=vxl_hybrid_payload_validate(VXL_HYBRID_CPU_BEGIN,p,128);if(rc)return rc;
 if(!h->enabled||!vxl_hybrid_mode(c))return -EACCES;
 if(!vxl_hunet_catalog_job(c->next_sequence))return -ESTALE;
 memcpy(&j,vxl_hunet_catalog_job(c->next_sequence),128);
 if(p->generation!=c->engine->generation||p->sequence!=j.sequence||p->stage!=j.stage_id||memcmp(p->run_id,c->run_id,16))return -ESTALE;
 if(p->dtype!=1||p->input_bytes!=(vxl_u64)j.tasks*4||p->output_bytes!=p->input_bytes||j.op!=6)return -EINVAL;
 if(!p->input_ptr||p->input_ptr>~(vxl_u64)0-p->input_bytes)return -EFAULT;
 vxl_hybrid_poll(c);if(c->engine->quarantined)return -EIO;
 if(h->phase==VXL_HYBRID_GPU_READY){
  if(p->token)return -ESTALE;
  rc=vxl_hybrid_enter_cpu(c);if(rc)return rc;
  h->begin_receipt=*p;h->begin_receipt.token=h->token;
  h->evidence.magic=VXL_HYBRID_EVIDENCE_MAGIC;h->evidence.version=1;h->evidence.bytes=384;h->evidence.kind=1;h->evidence.generation=p->generation;h->evidence.sequence=p->sequence;h->evidence.stage=p->stage;
  memcpy(h->evidence.run_id,p->run_id,16);h->evidence.token=h->token;h->evidence.input_bytes=p->input_bytes;h->evidence.output_bytes=p->output_bytes;h->evidence.algorithm=1;h->evidence.threads=4;h->evidence.gpu_jobs_before=c->engine->snapshot.jobs;h->evidence.gpu_jobs_after=c->engine->snapshot.jobs;
  memcpy(h->evidence.catalog_sha256,c->release->binding.jobs_sha256,32);h->evidence.owner_cookie=((vxl_u64)c->engine->generation<<32)^h->token;h->evidence.deadline_ns=h->deadline_ns;h->evidence.state=VXL_HYBRID_CPU_WAIT;
 }else{
  if(h->phase!=VXL_HYBRID_CPU_WAIT)return -EBUSY;
  rc=vxl_hybrid_transaction(c,p->run_id,p->generation,p->sequence,p->stage,p->token?p->token:h->token);if(rc)return rc;
 }
 return 0;
}
int vxl_hybrid_begin_cpu(struct vxl_hunet_ctx *c,struct vxl_hybrid_begin_v1 *p){
 struct vxl_hybrid_ctx *h;vxl_u64 start,ptr;int rc;
 if(!c||!p)return -EFAULT;
 h=&c->hybrid;start=vxl_hunet_mgmt_now(c);rc=begin_prepare(c,p);vxl_hunet_mgmt_mark(c,VXL_MGMT_VALIDATE,start,0);if(rc)return rc;
 if(c->mgmt_row)c->mgmt_row->key.token=h->token;
 rc=vxl_hybrid_snapshot_input(c);if(rc)return rc;
 start=vxl_hunet_mgmt_now(c);ptr=p->input_ptr;rc=c->engine->ops->write_user(c->engine->context,ptr,h->cpu_input,h->tensor_bytes);vxl_hunet_mgmt_mark(c,VXL_MGMT_COPY_USER,start,h->tensor_bytes);if(rc)return rc;
 *p=h->begin_receipt;p->input_ptr=ptr;return 0;
}
