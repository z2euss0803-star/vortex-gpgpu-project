#include <vxl/engine.h>
#include <vxl/hunet.h>
static int device_hash(struct vxl_hunet_ctx *c,vxl_u64 address,vxl_u64 bytes,vxl_u8 out[32],enum vxl_mgmt_field read_field,enum vxl_mgmt_field hash_field){
 struct vxl_hunet_digest_ctx s;vxl_u64 n,stamp;int rc;stamp=vxl_hunet_mgmt_now(c);rc=vxl_hunet_digest_init(c,&s);vxl_hunet_mgmt_mark(c,hash_field,stamp,0);if(rc)return rc;
 while(bytes){stamp=vxl_hunet_mgmt_now(c);vxl_hybrid_poll(c);vxl_hunet_mgmt_mark(c,VXL_MGMT_STATE,stamp,0);if(c->engine->quarantined){vxl_hunet_digest_abort(&s);return -ETIMEDOUT;}n=bytes>4096?4096:bytes;
  stamp=vxl_hunet_mgmt_now(c);rc=vxl_hunet_io_bytes(c,address,c->engine->scratch,n,0);vxl_hunet_mgmt_mark(c,read_field,stamp,n);if(rc){vxl_hunet_digest_abort(&s);return rc;}
  stamp=vxl_hunet_mgmt_now(c);rc=vxl_hunet_digest_update(&s,c->engine->scratch,n);vxl_hunet_mgmt_mark(c,hash_field,stamp,n);if(rc){vxl_hunet_digest_abort(&s);return rc;}address+=n;bytes-=n;
 }stamp=vxl_hunet_mgmt_now(c);rc=vxl_hunet_digest_final(&s,out);vxl_hunet_mgmt_mark(c,hash_field,stamp,0);vxl_hunet_digest_abort(&s);return rc;
}
static int failed(struct vxl_hunet_ctx *c,int rc){vxl_hybrid_abort(c,rc,c->hybrid.bytes_written!=0);return rc;}
static int commit_prepare(struct vxl_hunet_ctx *c,const struct vxl_hybrid_commit_v1 *p){
 struct vxl_hybrid_ctx *h;int rc;
 h=&c->hybrid;rc=vxl_hybrid_payload_validate(VXL_HYBRID_CPU_COMMIT,p,128);if(rc)return rc;
 rc=vxl_hybrid_transaction(c,p->run_id,p->generation,p->sequence,p->stage,p->token);if(rc)return rc;
 /* A successful transfer is durable even if its envelope could not be copied.
    The last exact commit stays retryable after evidence delivery. */
 if(h->commit_receipt.token){return memcmp(p,&h->commit_receipt,128)?-ESTALE:1;}
 if(h->phase!=VXL_HYBRID_CPU_WAIT||!h->input_ready)return -EBUSY;
 if(p->output_bytes!=h->tensor_bytes||p->cpu_duration_ns>60000000000ULL)return -EINVAL;
 if(!p->output_ptr||p->output_ptr>~(vxl_u64)0-p->output_bytes)return -EFAULT;
 return 0;
}
int vxl_hybrid_commit_cpu(struct vxl_hunet_ctx *c,const struct vxl_hybrid_commit_v1 *p){
 struct vxl_hybrid_ctx *h;struct vxl_hunet_job_v1 j;vxl_u8 sha[32];vxl_u32 word;vxl_u64 i,n,start,stamp;int rc;
 if(!c||!p)return -EFAULT;
 h=&c->hybrid;stamp=vxl_hunet_mgmt_now(c);rc=commit_prepare(c,p);vxl_hunet_mgmt_mark(c,VXL_MGMT_VALIDATE,stamp,0);if(rc)return rc==1?0:rc;
 if(!h->cpu_output){stamp=vxl_hunet_mgmt_now(c);h->cpu_output=vxl_hybrid_alloc((size_t)h->tensor_bytes);vxl_hunet_mgmt_mark(c,VXL_MGMT_ALLOC,stamp,h->tensor_bytes);if(!h->cpu_output)return failed(c,-ENOMEM);}
 stamp=vxl_hunet_mgmt_now(c);rc=c->engine->ops->read_user(c->engine->context,h->cpu_output,p->output_ptr,h->tensor_bytes);vxl_hunet_mgmt_mark(c,VXL_MGMT_SNAPSHOT,stamp,h->tensor_bytes);if(rc)return rc;
 /* All bytes are private before any device write. */
 stamp=vxl_hunet_mgmt_now(c);for(i=0;i<h->tensor_bytes;i+=4){memcpy(&word,h->cpu_output+i,4);if((word&0x7f800000)==0x7f800000){vxl_hunet_mgmt_mark(c,VXL_MGMT_FINITE,stamp,i+4);return -EDOM;}}
 vxl_hunet_mgmt_mark(c,VXL_MGMT_FINITE,stamp,h->tensor_bytes);stamp=vxl_hunet_mgmt_now(c);
 rc=vxl_hunet_digest_hash(c,h->cpu_output,h->tensor_bytes,sha);vxl_hunet_mgmt_mark(c,VXL_MGMT_HASH_OUTPUT,stamp,h->tensor_bytes);if(rc)return failed(c,rc);if(memcmp(sha,p->output_sha256,32))return -EACCES;
 h->evidence.check_flags|=VXL_HYBRID_CHECK_FINITE;memcpy(h->evidence.output_sha256,sha,32);h->evidence.user_cpu_ns=p->cpu_duration_ns;
 stamp=vxl_hunet_mgmt_now(c);rc=vxl_hybrid_idle(c);vxl_hunet_mgmt_mark(c,VXL_MGMT_IDLE,stamp,0);if(rc)return failed(c,rc);
 stamp=vxl_hunet_mgmt_now(c);memcpy(&j,vxl_hunet_catalog_job(h->sequence),128);rc=vxl_hunet_validate_spans(&j,NULL);vxl_hunet_mgmt_mark(c,VXL_MGMT_VALIDATE,stamp,0);if(rc||j.op!=6)return failed(c,-EACCES);
 start=c->io.now_ns(c->io.context);rc=device_hash(c,j.x,h->tensor_bytes,sha,VXL_MGMT_ORIGINAL_READ,VXL_MGMT_ORIGINAL_HASH);h->evidence.read_ns+=c->io.now_ns(c->io.context)-start;if(rc)return failed(c,rc);if(memcmp(sha,h->begin_receipt.input_sha256,32))return failed(c,-EIO);
 start=c->io.now_ns(c->io.context);rc=vxl_hunet_guards(c,0);h->evidence.guard_ns+=c->io.now_ns(c->io.context)-start;vxl_hunet_mgmt_mark(c,VXL_MGMT_GUARD,start,0);if(rc)return failed(c,rc);
 start=c->io.now_ns(c->io.context);
 for(i=0;i<h->tensor_bytes;i+=n){
  stamp=vxl_hunet_mgmt_now(c);vxl_hybrid_poll(c);vxl_hunet_mgmt_mark(c,VXL_MGMT_STATE,stamp,0);if(c->engine->quarantined)return failed(c,-ETIMEDOUT);n=h->tensor_bytes-i;if(n>4096)n=4096;
  stamp=vxl_hunet_mgmt_now(c);rc=vxl_hunet_io_bytes(c,(vxl_u64)j.y+i,h->cpu_output+i,n,1);vxl_hunet_mgmt_mark(c,VXL_MGMT_OUTPUT_WRITE,stamp,n);
  if(rc){h->evidence.write_ns=c->io.now_ns(c->io.context)-start;return failed(c,rc);}h->bytes_written+=n;
 }
 rc=device_hash(c,j.y,h->tensor_bytes,sha,VXL_MGMT_READBACK,VXL_MGMT_READBACK_HASH);h->evidence.write_ns=c->io.now_ns(c->io.context)-start;if(rc)return failed(c,rc);
 if(memcmp(sha,p->output_sha256,32))return failed(c,-EIO);
 h->evidence.check_flags|=VXL_HYBRID_CHECK_WRITEBACK;
 start=c->io.now_ns(c->io.context);rc=vxl_hunet_guards(c,0);h->evidence.guard_ns+=c->io.now_ns(c->io.context)-start;vxl_hunet_mgmt_mark(c,VXL_MGMT_GUARD,start,0);if(rc)return failed(c,rc);
 stamp=vxl_hunet_mgmt_now(c);rc=vxl_hybrid_idle(c);vxl_hunet_mgmt_mark(c,VXL_MGMT_IDLE,stamp,0);if(rc)return failed(c,rc);
 h->evidence.gpu_jobs_after=c->engine->snapshot.jobs;if(h->evidence.gpu_jobs_after!=h->evidence.gpu_jobs_before)return failed(c,-EIO);
 stamp=vxl_hunet_mgmt_now(c);vxl_hybrid_poll(c);vxl_hunet_mgmt_mark(c,VXL_MGMT_STATE,stamp,0);if(c->engine->quarantined)return failed(c,-ETIMEDOUT);
 stamp=vxl_hunet_mgmt_now(c);h->evidence.total_ns=c->io.now_ns(c->io.context)-h->begin_ns;h->evidence.state=VXL_HYBRID_CPU_COMMITTED;h->evidence.result=0;h->commit_receipt=*p;h->phase=VXL_HYBRID_CPU_COMMITTED;vxl_hunet_mgmt_mark(c,VXL_MGMT_FINALIZE,stamp,0);return 0;
}
