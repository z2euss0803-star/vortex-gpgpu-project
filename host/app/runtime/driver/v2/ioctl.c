#include <vxl/v2/dispatch.h>
static int owner_request(struct vxl_v2_port *p,vxl_u64 owner,const struct vxl_compute_v2_request *r){
 if(!owner || owner!=p->ctx->owner)return -EPERM;
 if(r->device_generation!=p->ctx->epoch)return -ESTALE;
 if(p->ctx->closed)return -EPIPE;
 return 0;
}
static int read_result(struct vxl_v2_port *p,vxl_u64 owner,struct vxl_compute_v2_request *r){
 struct vxl_v2_completion out;int rc;
 rc=vxl_v2_read_completion(p->ctx,owner,r->sequence,r->cookie,&out);
 if(rc)return rc;
 rc=p->ops->output(p->os,r->user_ptr,&out,sizeof(out));if(rc)return rc;
 r->sequence=out.sequence;r->cookie=out.cookie;r->state=out.state;r->result=out.result;return 0;
}
int vxl_v2_dispatch(struct vxl_v2_port *p,vxl_u64 owner,unsigned int cmd,vxl_u64 address){
 struct vxl_compute_v2_request r;struct vxl_v2_job *batch=0;vxl_u64 deadline=0,revision,first=0,now;
 unsigned int allowed=0;int rc,accepted=0;
 if(!p || !p->ctx || !p->ctx->queue || !p->ops)return -ENODEV;
 if(_IOC_TYPE(cmd)!='V' || _IOC_NR(cmd)<0x80 || _IOC_NR(cmd)>0x8b || _IOC_SIZE(cmd)!=128 || _IOC_DIR(cmd)!=(_IOC_READ|_IOC_WRITE))return -ENOTTY;
 rc=p->ops->input(p->os,&r,address,sizeof(r));if(rc)return rc;
 if(cmd==VXL_V2_COMPLETE_REAP)allowed=VXL_V2_READ|VXL_V2_ACK;
 rc=vxl_v2_request_valid(&r,allowed,0);if(rc)return rc;
 if(cmd==VXL_V2_SUBMIT_BATCH){
  if(!r.length || r.length>32 || r.payload_bytes!=r.length*512 || !r.user_ptr || r.sequence || r.cookie || r.handle || r.object_generation || r.offset)return -EINVAL;
  batch=p->ops->allocate(p->os,(vxl_u32)r.payload_bytes);if(!batch)return -ENOMEM;
  rc=p->ops->input(p->os,batch,r.user_ptr,(vxl_u32)r.payload_bytes);if(rc)goto free_batch;
 }else if(cmd==VXL_V2_WAIT || cmd==VXL_V2_COMPLETE_REAP){
  if((!r.sequence && !r.cookie) || r.handle || r.object_generation || r.offset)return -EINVAL;
  if(cmd==VXL_V2_WAIT){if(r.length>60000 || r.payload_bytes!=256 || !r.user_ptr)return -EINVAL;}
  else if(r.length || (r.flags!=VXL_V2_READ && r.flags!=VXL_V2_ACK) || (r.flags==VXL_V2_READ?(r.payload_bytes!=256 || !r.user_ptr):(r.payload_bytes || r.user_ptr)))return -EINVAL;
 }
 p->ops->lock(p->os);
 /* Bootstrap CAPS may query the current epoch; querying cannot advance it. */
 if(cmd==VXL_V2_CAPS && !r.device_generation)r.device_generation=p->ctx->epoch;
 rc=owner_request(p,owner,&r);if(rc)goto unlock;
 if(cmd==VXL_V2_SUBMIT_BATCH){
  first=p->ctx->queue->next_sequence+1;rc=vxl_v2_batch_accept(p->ctx,owner,batch,(vxl_u32)r.length);
  if(!rc){accepted=1;r.sequence=first;r.cookie=batch[0].cookie;r.state=VXL_V2_QUEUED;r.result=0;}
 }else if(cmd==VXL_V2_COMPLETE_REAP){
  if(r.flags==VXL_V2_READ)rc=read_result(p,owner,&r);
  else rc=vxl_v2_ack_reap(p->ctx,owner,r.sequence,r.cookie);
 }else if(cmd==VXL_V2_WAIT){
  now=p->ops->now_ns(p->os);
  if(r.length*1000000ULL>~(vxl_u64)0-now){rc=-EOVERFLOW;goto unlock;}deadline=now+r.length*1000000ULL;
  for(;;){
   rc=read_result(p,owner,&r);if(rc!=-EAGAIN)break;
   if(p->ops->now_ns(p->os)>=deadline){rc=-ETIMEDOUT;break;}
   revision=p->revision;p->ops->unlock(p->os);
   rc=p->ops->sleep(p->os,revision,deadline);
   p->ops->lock(p->os);
   if(rc && rc!=-ETIMEDOUT)break;
   rc=owner_request(p,owner,&r);if(rc)break;
   /* Re-read once after timeout: a completion racing the deadline is retained. */
  }
 }else if(cmd==VXL_V2_FAULT_INFO){
  if(r.handle || r.object_generation || r.offset || r.length || r.payload_bytes || r.user_ptr || r.sequence){rc=-EINVAL;goto unlock;}
  r.cookie=p->ctx->queue->fault_cookie;r.result=p->ctx->queue->fault_result;r.state=p->ctx->quarantined?VXL_V2_FAILED:0;rc=0;
 }else rc=p->control?p->control(p,owner,cmd,&r):-EOPNOTSUPP;
 if(!rc)rc=p->ops->output(p->os,address,&r,sizeof(r));
unlock:
 p->ops->unlock(p->os);
 if(accepted)p->ops->kick(p->os); /* Acceptance survives an envelope copy fault. */
free_batch:
 if(batch)p->ops->release(p->os,batch);
 return rc;
}
