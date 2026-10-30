#include <vxl/v2/jobs.h>
int vxl_v2_slot_find(struct vxl_v2_ctx *c,vxl_u64 owner,vxl_u64 sequence,vxl_u64 cookie,struct vxl_v2_slot **out){
 unsigned int i;
 if(!c || !c->queue || !out || (!sequence && !cookie))return -EINVAL;
 if(!owner || owner!=c->owner)return -EPERM;
 for(i=0;i<64;i++){
  struct vxl_v2_slot *s=&c->queue->slots[i];
  if(s->state==VXL_V2_SLOT_FREE || s->state==VXL_V2_STAGED)continue;
  if((sequence && s->completion.sequence==sequence) || (cookie && s->job.cookie==cookie)){
   if((sequence && s->completion.sequence!=sequence) || (cookie && s->job.cookie!=cookie))return -EINVAL;
   if(s->completion.device_generation!=c->epoch)return -ESTALE;
   *out=s;return 0;
  }
 }
 return (sequence && sequence<=c->queue->next_sequence) || (cookie && cookie<=c->queue->last_cookie)?-ESTALE:-ENOENT;
}
void vxl_v2_finish_slot(struct vxl_v2_queue *q,struct vxl_v2_slot *s,vxl_u32 state,int result,vxl_u64 now){
 if(s->state>=VXL_V2_OK && s->state<=VXL_V2_SKIPPED)return;
 s->completion.state=state;s->completion.result=result;s->completion.complete_ns=now;s->state=state;q->completed++;
}
int vxl_v2_read_completion(struct vxl_v2_ctx *c,vxl_u64 owner,vxl_u64 seq,vxl_u64 cookie,struct vxl_v2_completion *out){
 struct vxl_v2_slot *s;int rc;if(!out)return -EINVAL;rc=vxl_v2_slot_find(c,owner,seq,cookie,&s);if(rc)return rc;*out=s->completion;return s->state<VXL_V2_OK?-EAGAIN:0;
}
int vxl_v2_ack_reap(struct vxl_v2_ctx *c,vxl_u64 owner,vxl_u64 seq,vxl_u64 cookie){
 struct vxl_v2_slot *s;unsigned int i;int rc=vxl_v2_slot_find(c,owner,seq,cookie,&s);if(rc)return rc;
 if(c->quarantined)return -EIO;
 if(s->state<VXL_V2_OK)return -EAGAIN;
 for(i=0;i<s->ref_count;i++){rc=vxl_v2_buffer_unpin(c,owner,&s->refs[i],2);if(rc){c->quarantined=1;return rc;}}
 if(!s->program->pins){c->quarantined=1;return -EIO;}s->program->pins--;
 memset(s,0,sizeof(*s));c->queue->reserved--;c->queue->completed--;return 0;
}
vxl_u32 vxl_v2_poll_events(const struct vxl_v2_ctx *c){
 if(!c || !c->queue)return 0;
 return (c->queue->completed?1u:0u) | (c->quarantined || c->queue->fault_result?8u:0u);
}
int vxl_v2_deliver_completion(struct vxl_v2_ctx *c,vxl_u64 owner,vxl_u64 seq,vxl_u64 cookie,int (*write)(void *,const void *,vxl_u32),void *p){
 struct vxl_v2_completion out;int rc;if(!write)return -EINVAL;rc=vxl_v2_read_completion(c,owner,seq,cookie,&out);if(rc)return rc;return write(p,&out,sizeof(out));
}
